/*
 * test_remote_eval.c
 *
 * Verifies the remote line-eval console armed by
 * ruby_interpreter_enable_remote_eval():
 *   - listener comes up + cookie handshake succeeds + a real expression evals
 *   - empty token is refused at the API surface
 *   - wrong-token client is dropped server-side (no OK line)
 *   - a second concurrent client gets BUSY and is closed
 *   - no listener exists when enable_remote_eval was never called
 *   - `expose` + `attach` lets a script-registered Binding be used as the
 *     eval scope
 *
 * Same g_log pattern as test_remote_debug.c — diagnostics go to a file so
 * fd 1/2 (redirected into the logging pipe by the VM) can't deadlock the
 * logging thread via callback recursion.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <time.h>

#include "embedded-ruby-vm/ruby-api-loader.h"
#include "embedded-ruby-vm/assets-install.h"
#include "embedded-ruby-vm/assets-error.h"
#include "embedded-ruby-vm/ruby-vm-error.h"
#include "embedded-ruby-vm/log-listener.h"  /* logging_get_original_stdout_fd */

/* Use a high port unlikely to clash with anything in CI sandboxes. */
#define TEST_EVAL_PORT     57893
#define TEST_TOKEN         "test-cookie-please-ignore"
#define LISTENER_WAIT_MS   20000   /* upper bound for Ruby thread to bind */
#define SOCKET_READ_TIMEOUT_MS 3000

static RubyAPI ruby_api;
static AssetsLayout* g_layout = NULL;
static FILE* g_log = NULL;

/* LOGF must bypass the dup2 redirect once logging is initialized — the VM's
 * `ruby_vm_enable_logging` replaces STDOUT_FILENO with a pipe write end, so
 * any later `fprintf(stdout, ...)` from the test enters the async logging
 * pipeline. If the process dies before the logging thread drains, those
 * bytes are lost and the diagnostic trail goes dark exactly when we need
 * it. logging_emit_to_terminal() writes directly to the saved-original
 * stdout fd; before logging is initialized it returns -1 and we fall back
 * to fprintf(stdout). */
#define LOGF(fmt, ...) do {                                       \
    char _logf_buf[1024];                                         \
    int _logf_n = snprintf(_logf_buf, sizeof(_logf_buf), fmt, ##__VA_ARGS__); \
    if (_logf_n > 0) {                                            \
        size_t _logf_len = (size_t)_logf_n < sizeof(_logf_buf)    \
            ? (size_t)_logf_n : sizeof(_logf_buf) - 1;            \
        if (g_log) { fwrite(_logf_buf, 1, _logf_len, g_log); fflush(g_log); } \
        if (logging_emit_to_terminal(_logf_buf, _logf_len) < 0) { \
            fwrite(_logf_buf, 1, _logf_len, stdout); fflush(stdout); \
        }                                                         \
    }                                                             \
} while (0)

/* Dual-output the log callbacks so Ruby's own crash reports and the
 * `[Ruby VM] Failed ...` lines from exec-main-vm.c surface in CTest's
 * --output-on-failure dump (which only captures stdout/stderr — not the
 * separate test_remote_eval.log file). Without this, a fast process death
 * on the VM thread shows up as a silent failure in CI.
 *
 * Uses logging_emit_to_terminal() (the public safe-emit API) rather than
 * fprintf(stdout) — see log-listener.h for the why. */
static void emit_terminal(const char* prefix, const char* line) {
    char buf[2048];
    int n = snprintf(buf, sizeof(buf), "%s %s\n", prefix, line);
    if (n <= 0) return;
    size_t len = (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1;
    (void)logging_emit_to_terminal(buf, len);
}
static void OnRubyLog(LogListener* l, const char* line, log_stream_t source, log_level_t level) {
    (void)l;
    (void)source;
    const char* prefix = (level == LOG_LEVEL_ERROR) ? "[Ruby:err]" : "[Ruby]";
    if (g_log) { fprintf(g_log, "%s %s\n", prefix, line); fflush(g_log); }
    emit_terminal(prefix, line);
}

/* ---------- helpers ---------- */

static int try_tcp_connect(const char* host, int port, int timeout_ms) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        close(fd);
        return -1;
    }

    int rc = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
    if (rc == 0) return fd;
    if (errno != EINPROGRESS) { close(fd); return -1; }

    struct pollfd pfd = { .fd = fd, .events = POLLOUT };
    rc = poll(&pfd, 1, timeout_ms);
    if (rc <= 0) { close(fd); return -1; }
    int so_err = 0;
    socklen_t so_err_len = sizeof(so_err);
    getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_err, &so_err_len);
    if (so_err != 0) { close(fd); return -1; }
    return fd;
}

static int wait_for_listener(const char* host, int port, int total_timeout_ms) {
    const int step_ms = 100;
    int elapsed = 0;
    while (elapsed < total_timeout_ms) {
        int fd = try_tcp_connect(host, port, step_ms);
        if (fd >= 0) { close(fd); return 0; }
        usleep(step_ms * 1000);
        elapsed += step_ms;
        /* Heartbeat every 500 ms so a hang is visible in the log rather
         * than just a silent ~timeout. */
        if (elapsed % 500 == 0) {
            LOGF("  wait_for_listener: %d ms elapsed, still no listener on %s:%d\n",
                 elapsed, host, port);
        }
    }
    return -1;
}

/* Read up to dst_len-1 bytes or until we see a newline, with timeout. The
 * read is byte-by-byte because we need to stop AT the newline so subsequent
 * reads don't consume protocol bytes ahead of time. */
static int read_line(int fd, char* dst, size_t dst_len, int timeout_ms) {
    size_t pos = 0;
    while (pos < dst_len - 1) {
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        int pr = poll(&pfd, 1, timeout_ms);
        if (pr == 0) { return -1; }    /* timeout */
        if (pr < 0)  { if (errno == EINTR) continue; return -1; }

        char c;
        ssize_t n = read(fd, &c, 1);
        if (n == 0) break;             /* EOF — caller decides if OK */
        if (n < 0)  { if (errno == EINTR) continue; return -1; }
        if (c == '\n') break;
        dst[pos++] = c;
    }
    dst[pos] = '\0';
    return (int)pos;
}

static int read_prompt(int fd, int timeout_ms) {
    /* Prompt is "rubyvm[...]> " — no trailing newline. We just consume bytes
     * until we hit "> ". Bounded read avoids hanging. */
    char buf[128];
    size_t pos = 0;
    while (pos < sizeof(buf) - 1) {
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        int pr = poll(&pfd, 1, timeout_ms);
        if (pr <= 0) return -1;
        char c;
        ssize_t n = read(fd, &c, 1);
        if (n <= 0) return -1;
        buf[pos++] = c;
        if (pos >= 2 && buf[pos-2] == '>' && buf[pos-1] == ' ') {
            buf[pos] = '\0';
            return 0;
        }
    }
    return -1;
}

/* Send a literal NUL-terminated string. */
static int send_str(int fd, const char* s) {
    size_t len = strlen(s), pos = 0;
    while (pos < len) {
        ssize_t n = write(fd, s + pos, len - pos);
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        pos += (size_t)n;
    }
    return 0;
}

/* Drive the cookie handshake. Returns the connected fd on success, or -1
 * (with the fd closed). */
static int connect_and_auth(int port, const char* token) {
    int fd = try_tcp_connect("127.0.0.1", port, 1000);
    if (fd < 0) { LOGF("  connect failed\n"); return -1; }

    /* First line: "rubyvm-eval/1 token?" */
    char greeting[128];
    if (read_line(fd, greeting, sizeof(greeting), SOCKET_READ_TIMEOUT_MS) < 0) {
        LOGF("  no greeting received\n"); close(fd); return -1;
    }
    if (strstr(greeting, "rubyvm-eval/1") == NULL) {
        LOGF("  unexpected greeting: %s\n", greeting); close(fd); return -1;
    }

    /* Send token. */
    char token_line[256];
    snprintf(token_line, sizeof(token_line), "%s\n", token);
    if (send_str(fd, token_line) != 0) { close(fd); return -1; }

    /* Expect "OK" or close. */
    char reply[64];
    int n = read_line(fd, reply, sizeof(reply), SOCKET_READ_TIMEOUT_MS);
    if (n < 0)                  { LOGF("  no auth reply\n"); close(fd); return -1; }
    if (strcmp(reply, "OK") != 0) { LOGF("  auth rejected: '%s'\n", reply); close(fd); return -1; }
    return fd;
}

/* ---------- setup ---------- */

static int setup_runtime(void) {
    AssetsError err;
    assets_error_init(&err);
    g_layout = assets_bootstrap("./test-remote-eval-install", &err);
    if (!g_layout) { LOGF("assets_bootstrap failed: %s\n", err.message); return 1; }

    const char* deps_paths[] = {
        "../lib/libembedded-ruby.deps", "./libembedded-ruby.deps",
        "libembedded-ruby.deps", NULL
    };
    const char* lib_paths[] = {
        "../lib/libembedded-ruby.so", "./libembedded-ruby.so",
        "libembedded-ruby.so", NULL
    };
    if (ruby_api_bootstrap(&ruby_api, deps_paths, lib_paths, g_layout->native_libs_dir) != 0) {
        LOGF("ruby_api_bootstrap failed\n");
        return 2;
    }
    return 0;
}

static RubyInterpreter* make_interpreter(void) {
    LogListener listener = {
        .context = NULL, .user_data = NULL,
        .on_log_message = OnRubyLog,
    };
    return ruby_api.interpreter.create(".", g_layout->ruby_stdlib_path,
                                       g_layout->native_libs_dir, listener);
}

/* ---------- scenarios ---------- */

static int scenario_no_listener_without_enable(void) {
    LOGF("[test_remote_eval] scenario_no_listener_without_enable START\n");
    int fd = try_tcp_connect("127.0.0.1", TEST_EVAL_PORT + 2, 200);
    if (fd >= 0) {
        LOGF("  unexpected listener on port %d\n", TEST_EVAL_PORT + 2);
        close(fd);
        return 31;
    }
    LOGF("  OK: no listener on 127.0.0.1:%d (as expected)\n", TEST_EVAL_PORT + 2);
    return 0;
}

static int scenario_empty_token_refused(RubyInterpreter* interp) {
    LOGF("[test_remote_eval] scenario_empty_token_refused START\n");

    RubyVMRemoteEvalOptions opts = {
        .host = "127.0.0.1", .port = TEST_EVAL_PORT + 1,
        .token = "", .session_name = NULL,
    };
    int rc = ruby_api.interpreter.enable_remote_eval(interp, &opts);

    if (rc == RUBY_VM_OK)            { LOGF("  expected failure for empty token\n"); return 21; }
    if (rc != RUBY_VM_ERROR_INVALID_PARAM) {
        LOGF("  expected RUBY_VM_ERROR_INVALID_PARAM, got %d\n", rc);
        return 22;
    }
    LOGF("  OK: empty token rejected with INVALID_PARAM\n");
    return 0;
}

static int scenario_listener_up_and_eval(RubyInterpreter* interp) {
    LOGF("[test_remote_eval] scenario_listener_up_and_eval START\n");

    RubyVMRemoteEvalOptions opts = {
        .host = "127.0.0.1", .port = TEST_EVAL_PORT,
        .token = TEST_TOKEN, .session_name = "test_remote_eval",
    };
    LOGF("  step: calling enable_remote_eval(host=%s, port=%d, token=%s)...\n",
         opts.host, opts.port, opts.token);
    int rc = ruby_api.interpreter.enable_remote_eval(interp, &opts);
    LOGF("  step: enable_remote_eval returned rc=%d\n", rc);
    if (rc != RUBY_VM_OK) {
        LOGF("  enable_remote_eval failed: %d (%s)\n",
             rc, ruby_api.interpreter.get_error_message(interp));
        return 11;
    }

    LOGF("  step: polling wait_for_listener (timeout %d ms)...\n", LISTENER_WAIT_MS);
    if (wait_for_listener("127.0.0.1", TEST_EVAL_PORT, LISTENER_WAIT_MS) != 0) {
        LOGF("  no listener on 127.0.0.1:%d after %d ms\n", TEST_EVAL_PORT, LISTENER_WAIT_MS);
        return 12;
    }
    LOGF("  step: wait_for_listener succeeded\n");

    /* Real protocol round-trip. */
    int fd = connect_and_auth(TEST_EVAL_PORT, TEST_TOKEN);
    if (fd < 0) return 13;

    if (read_prompt(fd, SOCKET_READ_TIMEOUT_MS) != 0) {
        LOGF("  no prompt after OK\n"); close(fd);
        return 14;
    }

    if (send_str(fd, "1 + 1\n") != 0) {
        LOGF("  failed to send eval\n"); close(fd);
        return 15;
    }

    char reply[128];
    if (read_line(fd, reply, sizeof(reply), SOCKET_READ_TIMEOUT_MS) < 0) {
        LOGF("  no eval reply\n"); close(fd);
        return 16;
    }
    if (strcmp(reply, "=> 2") != 0) {
        LOGF("  unexpected eval reply: '%s' (want '=> 2')\n", reply); close(fd);
        return 17;
    }
    LOGF("  OK: 1 + 1 => 2\n");

    /* Clean exit. */
    send_str(fd, "exit\n");
    close(fd);
    return 0;
}

/* Reuses the listener that scenario_listener_up_and_eval armed. The VM and
 * its eval thread stay alive across scenarios because main() holds a single
 * interpreter for the lifetime of the test (so the log listener stays
 * attached and Ruby-side errors remain visible if the listener thread dies).
 */
static int scenario_wrong_token_dropped(void) {
    LOGF("[test_remote_eval] scenario_wrong_token_dropped START\n");

    int fd = try_tcp_connect("127.0.0.1", TEST_EVAL_PORT, 1000);
    if (fd < 0) {
        LOGF("  connect to existing listener failed (listener gone?)\n");
        return 41;
    }

    /* Consume the greeting. */
    char greeting[128];
    if (read_line(fd, greeting, sizeof(greeting), SOCKET_READ_TIMEOUT_MS) < 0) {
        LOGF("  no greeting\n"); close(fd); return 42;
    }

    /* Send the wrong cookie. */
    send_str(fd, "definitely-not-the-token\n");

    /* Server should close without sending OK. read_line returns -1 (timeout)
     * or 0 (clean EOF) — either way, we must NOT see "OK". */
    char reply[64];
    int n = read_line(fd, reply, sizeof(reply), SOCKET_READ_TIMEOUT_MS);
    if (n > 0 && strcmp(reply, "OK") == 0) {
        LOGF("  server accepted a wrong cookie! got: '%s'\n", reply);
        close(fd); return 43;
    }
    LOGF("  OK: wrong-token connection was dropped (read got n=%d)\n", n);
    close(fd);
    return 0;
}

/* Sends a Ruby script via the FIFO interpreter that calls
 * RemoteEval.expose(:probe, binding) where the binding has a local
 * variable `secret = 1337`. Then opens an eval session, runs
 * `attach probe`, then `secret`, and asserts the response is `=> 1337`. */
static int scenario_expose_and_attach(RubyInterpreter* interp) {
    LOGF("[test_remote_eval] scenario_expose_and_attach START\n");

    /* Script wraps its locals in a Proc whose binding we expose, so the
     * local `secret` survives after the eval'd script returns. */
    const char* script_src =
        "secret = 1337\n"
        "RemoteEval.expose(:probe, binding)\n";
    RubyScript* script = ruby_api.script.create_from_content(script_src, strlen(script_src));
    if (!script) return 51;

    int sync_rc = ruby_api.interpreter.execute_sync(interp, script);
    ruby_api.script.destroy(script);
    if (sync_rc != 0) {
        LOGF("  exposing script failed: %d\n", sync_rc);
        return 52;
    }

    /* Now drive the remote console. */
    int fd = connect_and_auth(TEST_EVAL_PORT, TEST_TOKEN);
    if (fd < 0) return 53;
    if (read_prompt(fd, SOCKET_READ_TIMEOUT_MS) != 0) {
        close(fd); return 54;
    }

    send_str(fd, "attach probe\n");
    char reply[256];
    if (read_line(fd, reply, sizeof(reply), SOCKET_READ_TIMEOUT_MS) < 0) {
        close(fd); return 55;
    }
    if (strstr(reply, "attached to :probe") == NULL) {
        LOGF("  unexpected attach reply: '%s'\n", reply);
        close(fd); return 56;
    }
    if (read_prompt(fd, SOCKET_READ_TIMEOUT_MS) != 0) {
        close(fd); return 57;
    }

    send_str(fd, "secret\n");
    if (read_line(fd, reply, sizeof(reply), SOCKET_READ_TIMEOUT_MS) < 0) {
        close(fd); return 58;
    }
    if (strcmp(reply, "=> 1337") != 0) {
        LOGF("  unexpected eval reply for 'secret': '%s' (want '=> 1337')\n", reply);
        close(fd); return 59;
    }
    LOGF("  OK: attached to :probe, secret => 1337\n");

    send_str(fd, "exit\n");
    close(fd);
    return 0;
}

/* Both listeners armed on the same VM. enable_remote_eval already brought
 * the eval listener up via the boot path (since the VM was idle when this
 * binary started). Now we ask enable_remote_debug to arm the DAP listener
 * on a different port — which exercises the post-boot inject path. */
static int scenario_coexist_with_debug(RubyInterpreter* interp) {
    LOGF("[test_remote_eval] scenario_coexist_with_debug START\n");

    int debug_port = TEST_EVAL_PORT + 5;
    RubyVMRemoteDebugOptions opts = {
        .host = "127.0.0.1", .port = debug_port,
        .token = TEST_TOKEN, .session_name = "coexist",
    };
    int rc = ruby_api.interpreter.enable_remote_debug(interp, &opts);
    if (rc != RUBY_VM_OK) {
        LOGF("  enable_remote_debug post-boot failed: %d (%s)\n",
             rc, ruby_api.interpreter.get_error_message(interp));
        return 61;
    }

    if (wait_for_listener("127.0.0.1", debug_port, LISTENER_WAIT_MS) != 0) {
        LOGF("  DAP listener never bound on port %d\n", debug_port);
        return 62;
    }
    LOGF("  OK: DAP listener up on %d (via post-boot inject)\n", debug_port);

    /* And the eval listener from earlier is still up. */
    int fd = try_tcp_connect("127.0.0.1", TEST_EVAL_PORT, 500);
    if (fd < 0) {
        LOGF("  eval listener regressed after debug injection?\n");
        return 63;
    }
    close(fd);
    LOGF("  OK: eval listener still up on %d (both coexisting)\n", TEST_EVAL_PORT);

    return 0;
}

/* ---------- driver ---------- */

int main(void) {
    /* Unbuffered output: a crash in a sibling thread (VM thread, logging
     * thread, signal handler) shouldn't swallow lines that were already
     * printed but not flushed. Cheap; test-only. */
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    g_log = fopen("test_remote_eval.log", "w");
    if (!g_log) {
        fprintf(stdout, "WARNING: could not open test_remote_eval.log: %s\n", strerror(errno));
    } else {
        fprintf(stdout, "[test_remote_eval] diagnostic log: ./test_remote_eval.log\n");
        fflush(stdout);
    }

    int rc = setup_runtime();
    if (rc != 0) return rc;

    /* Order matters: no-listener-without-enable must run BEFORE any scenario
     * that boots the VM. After that, the single-shot lifecycle means only
     * one scenario actually gets to start a listener — subsequent ones
     * either skip cleanly or piggyback.
     *
     * One interpreter wrapper is held alive for the whole test run so the
     * VM's LogListener stays bound to OnRubyLog. ruby_interpreter_destroy
     * swaps the listener to empty when called mid-flow, which silently
     * dropped every Ruby-side log line between scenarios — including any
     * `[RemoteEval] listener crashed: ...` / `listener thread exiting ...`
     * that would tell us why the eval listener died on macOS. Keep one
     * interpreter, destroy at the very end. */
    rc = scenario_no_listener_without_enable();
    if (rc != 0) { LOGF("FAIL: no_listener_without_enable (%d)\n", rc); return rc; }

    RubyInterpreter* interp = make_interpreter();
    if (!interp) { LOGF("FAIL: make_interpreter at startup\n"); return 100; }

    rc = scenario_empty_token_refused(interp);
    if (rc != 0) { LOGF("FAIL: empty_token_refused (%d)\n", rc); goto done; }

    rc = scenario_listener_up_and_eval(interp);
    if (rc != 0) { LOGF("FAIL: listener_up_and_eval (%d)\n", rc); goto done; }

    rc = scenario_wrong_token_dropped();
    if (rc != 0) { LOGF("FAIL: wrong_token_dropped (%d)\n", rc); goto done; }

    rc = scenario_expose_and_attach(interp);
    if (rc != 0) { LOGF("FAIL: expose_and_attach (%d)\n", rc); goto done; }

    rc = scenario_coexist_with_debug(interp);
    if (rc != 0) { LOGF("FAIL: coexist_with_debug (%d)\n", rc); goto done; }

    LOGF("[test_remote_eval] ALL OK\n");
done:
    ruby_api.interpreter.destroy(interp);
    return rc;
}
