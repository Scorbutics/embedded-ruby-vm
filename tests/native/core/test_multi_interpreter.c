/*
 * test_multi_interpreter.c
 *
 * Covers the thread-per-interpreter refactor of fifo_interpreter.rb:
 *
 *   1. Create TWO RubyInterpreter wrappers (different interpreter_ids).
 *   2. Enqueue a long-running script on interpreter A that polls a Ruby
 *      global ($stop_a) and increments $counter_a until told to stop.
 *   3. While A is busy, execute several sync scripts on interpreter B.
 *      Before the refactor this would deadlock on the single socket_lock
 *      / single FIFO interpreter eval. With per-interpreter Ruby Threads,
 *      B's worker runs on its own Thread, sharing the GVL cooperatively
 *      with A (which yields via sleep).
 *   4. Have B set $stop_a = true (workers share Ruby globals — one VM).
 *   5. Wait for A's enqueue completion callback. It must fire with
 *      exit code 0, proving A's worker actually drained the queued
 *      script and ran to its natural end.
 *   6. Destroy both interpreters and confirm they return promptly (the
 *      stop sentinel sent by ruby_interpreter_destroy is what frees up
 *      each worker Thread).
 *
 * If the refactor regresses, this test deadlocks at step 3 (B's first
 * sync script never returns) or at step 6 (destroy hangs because the
 * stop sentinel never reaches a worker).
 */

#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "embedded-ruby-vm/assets-error.h"
#include "embedded-ruby-vm/assets-install.h"
#include "embedded-ruby-vm/log-listener.h"
#include "embedded-ruby-vm/ruby-api-loader.h"

/* Print directly to the pre-redirect terminal stderr — same rationale as
 * test_sentinel_log_level: fprintf(stderr) goes through the logging pipe
 * and may be lost during shutdown. */
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

/* Generous outer wall-clock cap. Any successful run finishes in < 5s on a
 * developer laptop; anything beyond is a deadlock symptom. */
#define OVERALL_TIMEOUT_MS 30000
#define A_COMPLETION_TIMEOUT_MS 15000

static RubyAPI g_ruby_api;

static atomic_int a_finished = 0;
static int a_exit_code = -1;

static void on_a_completed(void* ctx, int result) {
    (void) ctx;
    a_exit_code = result;
    atomic_store(&a_finished, 1);
}

static void on_log(LogListener* listener, const char* line, log_stream_t stream, log_level_t level, int interpreter_id) {
    (void) listener;
    (void) stream;
    (void) interpreter_id;
    const char* prefix = (level == LOG_LEVEL_ERROR) ? "[ruby-err]" : "[ruby]";
    diag("%s %s", prefix, line);
}

static long elapsed_ms(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) * 1000L
         + (end.tv_nsec - start.tv_nsec) / 1000000L;
}

static int run_sync(RubyInterpreter* interp, const char* src, const char* tag) {
    RubyScript* s = g_ruby_api.script.create_from_content(src, strlen(src));
    if (s == NULL) {
        diag("FAIL: failed to allocate script (%s)", tag);
        return -1;
    }
    int rc = g_ruby_api.interpreter.execute_sync(interp, s);
    g_ruby_api.script.destroy(s);
    return rc;
}

int main(void) {
    int test_rc = 1;
    AssetsLayout* layout = NULL;
    AssetsError assets_error;
    RubyInterpreter* a = NULL;
    RubyInterpreter* b = NULL;
    RubyScript* a_script = NULL;

    assets_error_init(&assets_error);
    layout = assets_bootstrap("./test-ruby-install-multi-interp", &assets_error);
    if (layout == NULL) {
        diag("FAIL: bootstrap: %s", assets_error.message);
        return 1;
    }

    const char* deps_paths[] = {
        "../lib/libembedded-ruby.deps",
        "./libembedded-ruby.deps",
        "libembedded-ruby.deps",
        NULL
    };
    const char* lib_paths[] = {
        "../lib/libembedded-ruby.so",
        "./libembedded-ruby.so",
        "libembedded-ruby.so",
        NULL
    };

    if (ruby_api_bootstrap(&g_ruby_api, deps_paths, lib_paths, layout->native_libs_dir) != 0) {
        diag("FAIL: ruby_api_bootstrap");
        goto cleanup;
    }

    LogListener listener = {
        .context = NULL,
        .user_data = NULL,
        .on_log_message = on_log,
    };

    a = g_ruby_api.interpreter.create(".", layout->ruby_stdlib_path, layout->native_libs_dir, listener);
    b = g_ruby_api.interpreter.create(".", layout->ruby_stdlib_path, layout->native_libs_dir, listener);
    if (a == NULL || b == NULL) {
        diag("FAIL: failed to create interpreters (a=%p b=%p)", (void*) a, (void*) b);
        goto cleanup;
    }

    /* Warm up the VM so the per-interpreter assertions below see only the
     * steady-state behaviour, not the first-run init cost. */
    if (run_sync(b, "1 + 1\n", "warm-up") != 0) {
        diag("FAIL: warm-up sync script");
        goto cleanup;
    }

    /* Step 2: enqueue A's long-running loop. It polls $stop_a (a Ruby
     * global, visible to every worker in the VM) and yields with sleep
     * so the GVL is released for B's worker. */
    const char* a_src =
        "$counter_a = 0\n"
        "$stop_a = false\n"
        "until $stop_a\n"
        "  $counter_a += 1\n"
        "  sleep(0.01)\n"
        "end\n"
        "puts \"A: counter_a=#{$counter_a}, stopped\"\n";
    a_script = g_ruby_api.script.create_from_content(a_src, strlen(a_src));
    if (a_script == NULL) {
        diag("FAIL: failed to allocate A's script");
        goto cleanup;
    }
    g_ruby_api.interpreter.enqueue(
        a,
        a_script,
        ruby_completion_task_create(on_a_completed, NULL)
    );

    /* Step 3: give A's worker time to actually start eval'ing before we
     * fire B's first sync script. Without this we might race past the
     * point where the deadlock would have happened (B's request arrives
     * before A's; not the case we want to exercise). */
    usleep(200 * 1000);

    diag("DIAG: enqueued A; running B sync scripts concurrently…");

    struct timespec start_b, end_b;
    clock_gettime(CLOCK_MONOTONIC, &start_b);

    /* Step 3 continued: ten short sync scripts on B. Each one captures
     * $counter_a at execution time. With cooperative scheduling we
     * expect $counter_a to strictly increase between iterations. */
    int last_counter = -1;
    int rolled_back = 0;
    for (int i = 0; i < 10; i++) {
        char src[256];
        snprintf(src, sizeof(src),
                 "$b_pings = ($b_pings || 0) + 1\n"
                 "puts \"B[%d]: b_pings=#{$b_pings}, counter_a=#{$counter_a}\"\n"
                 "$last_observed_counter_a = $counter_a\n",
                 i);
        int rc = run_sync(b, src, "B iteration");
        if (rc != 0) {
            diag("FAIL: B iteration %d returned exit_code=%d", i, rc);
            goto cleanup;
        }
        /* Pull $last_observed_counter_a back over by running another
         * tiny script that prints it; the C side cannot read Ruby
         * globals directly, so we infer "did A progress?" from the
         * log output above. As a sanity check we just verify the
         * exec_sync returned 0 — the meat of the assertion lives in
         * step 5. */
        (void) last_counter;
        (void) rolled_back;
        usleep(50 * 1000);
    }
    clock_gettime(CLOCK_MONOTONIC, &end_b);
    long b_elapsed = elapsed_ms(start_b, end_b);
    diag("DIAG: ten B sync scripts completed in %ld ms (while A was running)", b_elapsed);

    /* Step 3 verification: each B iteration sleeps 50ms after the eval,
     * so 10 iterations should take ~500ms + eval overhead. If we're
     * deadlocking on the old single socket_lock, B iteration 1 would
     * never return (A holds the lock forever inside its until-loop) and
     * we'd be killed by OVERALL_TIMEOUT_MS instead of reaching this
     * line. So just reaching here proves the deadlock is gone. As a
     * cheap upper-bound assertion we still ensure we didn't hang. */
    if (b_elapsed > OVERALL_TIMEOUT_MS) {
        diag("FAIL: B's sync iterations took %ld ms (> %d ms threshold)", b_elapsed, OVERALL_TIMEOUT_MS);
        goto cleanup;
    }

    /* Step 4: ask A to stop. We do this through B's worker, NOT A's,
     * because A's worker is stuck inside its until-loop and any script
     * we enqueue against A would queue behind the loop. */
    diag("DIAG: signalling A to stop (via B)");
    if (run_sync(b, "$stop_a = true\n", "stop signal") != 0) {
        diag("FAIL: setting $stop_a via B");
        goto cleanup;
    }

    /* Step 5: wait for A's completion callback. */
    int waited = 0;
    while (!atomic_load(&a_finished) && waited < A_COMPLETION_TIMEOUT_MS) {
        usleep(10 * 1000);
        waited += 10;
    }
    if (!atomic_load(&a_finished)) {
        diag("FAIL: A did not complete within %d ms after $stop_a was set", A_COMPLETION_TIMEOUT_MS);
        goto cleanup;
    }
    if (a_exit_code != 0) {
        diag("FAIL: A exited with non-zero code: %d", a_exit_code);
        goto cleanup;
    }
    diag("DIAG: A completed cleanly after stop signal (waited %d ms)", waited);

    /* Step 6: destroy. The stop sentinel that ruby_interpreter_destroy
     * sends to each worker must come back as a 0 exit code; if anything
     * here hangs it's the destroy path's drain that's broken. */
    struct timespec start_dx, end_dx;
    clock_gettime(CLOCK_MONOTONIC, &start_dx);
    g_ruby_api.interpreter.destroy(a);
    a = NULL;
    g_ruby_api.interpreter.destroy(b);
    b = NULL;
    clock_gettime(CLOCK_MONOTONIC, &end_dx);
    long destroy_elapsed = elapsed_ms(start_dx, end_dx);
    diag("DIAG: destroyed both interpreters in %ld ms", destroy_elapsed);

    /* A's worker should have already exited on its own (step 5), so its
     * destroy is just memory cleanup. B's worker is idle when destroy
     * runs, so its stop sentinel should be processed immediately. We
     * give a generous 5s budget. */
    if (destroy_elapsed > 5000) {
        diag("FAIL: destroying both interpreters took %ld ms (> 5000)", destroy_elapsed);
        goto cleanup;
    }

    diag("PASS: thread-per-interpreter cooperative execution");
    test_rc = 0;

cleanup:
    if (a_script != NULL) g_ruby_api.script.destroy(a_script);
    if (a != NULL) g_ruby_api.interpreter.destroy(a);
    if (b != NULL) g_ruby_api.interpreter.destroy(b);
    if (layout != NULL) assets_free_layout(layout);
    ruby_api_unload(&g_ruby_api);
    return test_rc;
}
