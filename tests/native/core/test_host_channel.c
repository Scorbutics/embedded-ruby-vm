/*
 * VMHost round trip on a real Ruby VM (vm_host.rb + the C event dispatch).
 * Scripts fail by raising: a worker counts any exit() as a failure.
 *
 *   1. VMHost.emit reaches the LogListener of the interpreter running the
 *      script, as LOG_STREAM_HOST_EVENT "<topic> <base64>" (UTF-8 intact).
 *   2. VMHost.emit rejects an invalid topic.
 *   3. A 10 KB event, sent in PART lines while another Thread logs, arrives
 *      whole.
 *   4. An event from a Fiber of the worker is routed to its interpreter;
 *      VMHost.emit from a Thread the script spawned (no owner) raises.
 *   5. The host's messages reach a long-running script: interpreter "game"
 *      runs a loop reading VMHost.each_message('loop.'); interpreter "host"
 *      runs VMHost.deliver synchronously, as the Kotlin `post` does; the
 *      loop echoes each message with VMHost.emit, in the order posted.
 *   6. each_message only takes the topics under its prefix.
 *   7. VMHost.deliver refuses a message beyond MAX_PENDING (post fails).
 *   8. When the each_message block raises, the messages it did not get yet
 *      stay pending.
 */

#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "embedded-ruby-vm/assets-error.h"
#include "embedded-ruby-vm/assets-install.h"
#include "embedded-ruby-vm/log-listener.h"
#include "embedded-ruby-vm/ruby-api-loader.h"

static void diag(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf) - 1, fmt, args);
    va_end(args);
    if (n <= 0) return;
    if (n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;
    buf[n++] = '\n';
    int fd = logging_get_original_stderr_fd();
    if (fd < 0) fd = 2;
    ssize_t ignored = write(fd, buf, (size_t)n);
    (void)ignored;
}

#define MAX_EVENTS 16

typedef struct {
    char* events[MAX_EVENTS];
    atomic_int count;
} events_t;

static RubyAPI g_ruby_api;
static events_t g_game_events, g_host_events;
static atomic_int g_loop_finished = 0;
static int g_loop_exit_code = -1;

static void on_log(LogListener* listener, const char* line, log_stream_t stream, log_level_t level, int interpreter_id) {
    (void)interpreter_id;
    if (stream != LOG_STREAM_HOST_EVENT) {
        if (level == LOG_LEVEL_ERROR) diag("[ruby-err] %s", line);
        return;
    }
    events_t* events = (events_t*)listener->user_data;
    int index = atomic_load(&events->count);
    if (index < MAX_EVENTS) events->events[index] = strdup(line);
    atomic_store(&events->count, index + 1);
}

static void on_loop_completed(void* ctx, int result) {
    (void)ctx;
    g_loop_exit_code = result;
    atomic_store(&g_loop_finished, 1);
}

static int wait_events(events_t* events, int expected) {
    for (int i = 0; i < 500; i++) {
        if (atomic_load(&events->count) >= expected) return 1;
        usleep(10 * 1000);
    }
    return 0;
}

static int check_event(events_t* events, int index, const char* expected) {
    if (atomic_load(&events->count) <= index || strcmp(events->events[index], expected) != 0) {
        diag("FAIL: event %d is '%.80s', expected '%.80s'", index,
             atomic_load(&events->count) > index ? events->events[index] : "(none)", expected);
        return 0;
    }
    return 1;
}

static int run_sync(RubyInterpreter* interp, const char* src) {
    RubyScript* s = g_ruby_api.script.create_from_content(src, strlen(src));
    if (s == NULL) return -1;
    int rc = g_ruby_api.interpreter.execute_sync(interp, s);
    g_ruby_api.script.destroy(s);
    return rc;
}

int main(void) {
    int test_rc = 1;
    RubyInterpreter* game = NULL;
    RubyInterpreter* host = NULL;
    RubyScript* loop_script = NULL;
    char* big_event = NULL;
    AssetsError assets_error;
    assets_error_init(&assets_error);
    AssetsLayout* layout = assets_bootstrap("./test-ruby-install-host-channel", &assets_error);
    if (layout == NULL) {
        diag("FAIL: bootstrap: %s", assets_error.message);
        return 1;
    }
    const char* deps_paths[] = { "../lib/libembedded-ruby.deps", "./libembedded-ruby.deps", "libembedded-ruby.deps", NULL };
    const char* lib_paths[] = { "../lib/libembedded-ruby.so", "./libembedded-ruby.so", "libembedded-ruby.so", NULL };
    if (ruby_api_bootstrap(&g_ruby_api, deps_paths, lib_paths, layout->native_libs_dir) != 0) {
        diag("FAIL: ruby_api_bootstrap");
        goto cleanup;
    }

    LogListener game_listener = { .context = NULL, .user_data = &g_game_events, .on_log_message = on_log };
    LogListener host_listener = { .context = NULL, .user_data = &g_host_events, .on_log_message = on_log };
    game = g_ruby_api.interpreter.create(".", layout->ruby_stdlib_path, layout->native_libs_dir, game_listener);
    host = g_ruby_api.interpreter.create(".", layout->ruby_stdlib_path, layout->native_libs_dir, host_listener);
    if (game == NULL || host == NULL) {
        diag("FAIL: interpreter creation");
        goto cleanup;
    }

    /* Case 1: "h\u00e9 \u{1F600}" is aMOpIPCfmIA= */
    if (run_sync(game, "raise 'no host' unless VMHost.emit('t.hello', \"h\\u00e9 \\u{1F600}\")\n") != 0 ||
        !wait_events(&g_game_events, 1) || !check_event(&g_game_events, 0, "t.hello aMOpIPCfmIA=")) {
        diag("FAIL: emitted event");
        goto cleanup;
    }

    /* Case 2 */
    if (run_sync(game, "VMHost.emit('bad topic', '')\n") == 0) {
        diag("FAIL: an invalid topic was accepted");
        goto cleanup;
    }

    /* Case 3: "x" * 10000 is "eHh4" * 3333 + "eA==" */
    if (run_sync(game,
                 "logger = Thread.new { 200.times { |i| VMLogger.info(\"noise #{i}\") } }\n"
                 "VMHost.emit('big.event', 'x' * 10_000)\n"
                 "logger.join\n") != 0) {
        diag("FAIL: big event script");
        goto cleanup;
    }
    big_event = malloc(10 + 13336 + 1);
    memcpy(big_event, "big.event ", 10);
    for (int i = 0; i < 3333; i++) memcpy(big_event + 10 + i * 4, "eHh4", 4);
    strcpy(big_event + 10 + 3333 * 4, "eA==");
    if (!wait_events(&g_game_events, 2) || !check_event(&g_game_events, 1, big_event)) {
        diag("FAIL: big event (%d events)", atomic_load(&g_game_events.count));
        goto cleanup;
    }

    /* Case 4 */
    if (run_sync(game,
                 "begin\n"
                 "  Thread.new { VMHost.emit('t.thread', '') }.join\n"
                 "  raise 'emit from a spawned Thread accepted'\n"
                 "rescue ThreadError\n"
                 "end\n"
                 "Fiber.new { VMHost.emit('t.fiber', '') }.resume\n") != 0 ||
        !wait_events(&g_game_events, 3) || !check_event(&g_game_events, 2, "t.fiber ")) {
        diag("FAIL: fiber event (%d events)", atomic_load(&g_game_events.count));
        goto cleanup;
    }

    /* Case 5: the game loop (ends after 10 s anyway, so a failure fails
     * instead of hanging the destroy below) */
    const char* loop_src =
        "done = false\n"
        "deadline = Time.now + 10\n"
        "until done || Time.now > deadline\n"
        "  VMHost.each_message('loop.') do |topic, payload|\n"
        "    VMHost.emit('loop.echo', \"#{topic}=#{payload}\")\n"
        "    done = payload == 'stop'\n"
        "  end\n"
        "  sleep(0.01)\n"
        "end\n"
        "raise 'no stop message' unless done\n";
    loop_script = g_ruby_api.script.create_from_content(loop_src, strlen(loop_src));
    g_ruby_api.interpreter.enqueue(game, loop_script, ruby_completion_task_create(on_loop_completed, NULL));

    /* What `post` writes: the payload as base64 ("one", "other", "stop") */
    if (run_sync(host, "VMHost.deliver('loop.msg', 'b25l'.unpack1('m0'))\n") != 0 ||
        run_sync(host, "VMHost.deliver('other.msg', 'b3RoZXI='.unpack1('m0'))\n") != 0 ||
        run_sync(host, "VMHost.deliver('loop.msg', 'c3RvcA=='.unpack1('m0'))\n") != 0) {
        diag("FAIL: deliver scripts");
        goto cleanup;
    }
    for (int i = 0; i < 1500 && !atomic_load(&g_loop_finished); i++) usleep(10 * 1000);
    if (!atomic_load(&g_loop_finished) || g_loop_exit_code != 0) {
        diag("FAIL: the game loop did not end on 'stop' (finished=%d exit=%d)",
             atomic_load(&g_loop_finished), g_loop_exit_code);
        goto cleanup;
    }
    if (!wait_events(&g_game_events, 5) ||
        !check_event(&g_game_events, 3, "loop.echo bG9vcC5tc2c9b25l") ||
        !check_event(&g_game_events, 4, "loop.echo bG9vcC5tc2c9c3RvcA==")) {
        goto cleanup;
    }

    /* Case 6: other.msg is still pending, for its own consumer */
    if (run_sync(host, "n = VMHost.each_message('other.') { |_, p| raise p unless p == 'other' }\n"
                       "raise \"took #{n}\" unless n == 1\n") != 0) {
        diag("FAIL: the other consumer's message was taken");
        goto cleanup;
    }

    /* Case 7 */
    if (run_sync(host, "VMHost::MAX_PENDING.times { |i| VMHost.deliver('full.msg', i.to_s) }\n") != 0 ||
        run_sync(host, "VMHost.deliver('full.msg', 'one too many')\n") == 0 ||
        run_sync(host, "n = VMHost.each_message('full.') {}\n"
                       "raise \"took #{n}\" unless n == VMHost::MAX_PENDING\n") != 0) {
        diag("FAIL: MAX_PENDING bound");
        goto cleanup;
    }

    /* Case 8: '2' raises, so '1' is handled, '2' is lost with its error, '3' stays */
    if (run_sync(host, "%w[1 2 3].each { |p| VMHost.deliver('requeue.msg', p) }\n"
                       "begin\n"
                       "  VMHost.each_message('requeue.') { |_, p| raise 'boom' if p == '2' }\n"
                       "rescue RuntimeError\n"
                       "end\n"
                       "left = []\n"
                       "VMHost.each_message('requeue.') { |_, p| left << p }\n"
                       "raise \"left #{left}\" unless left == ['3']\n") != 0) {
        diag("FAIL: requeue after a raising block");
        goto cleanup;
    }

    if (atomic_load(&g_host_events.count) != 0) {
        diag("FAIL: %d events reached the host interpreter's listener", atomic_load(&g_host_events.count));
        goto cleanup;
    }

    diag("PASS: test_host_channel");
    test_rc = 0;

cleanup:
    if (game) g_ruby_api.interpreter.destroy(game);
    if (host) g_ruby_api.interpreter.destroy(host);
    if (loop_script) g_ruby_api.script.destroy(loop_script);
    free(big_event);
    assets_free_layout(layout);
    ruby_api_unload(&g_ruby_api);
    return test_rc;
}
