/*
 * test_same_id_fifo.c
 *
 * Regression test for the per-VM result_waiter FIFO list in ruby-vm.c.
 *
 * Architectural invariant being verified:
 *   "FIFO order on the list mirrors FIFO order on the wire, which mirrors
 *    FIFO order in the dispatcher's per-id worker queue. So 'first waiter
 *    with matching id' is always the right one."
 *   (see ruby-vm.c result_waiter docstring)
 *
 * test_multi_interpreter exercises the easy case: two distinct
 * interpreter_ids, where any pop-by-id implementation works because
 * responses can't be confused with each other. This test exercises the
 * hard case: N concurrent submitters all targeting the SAME interpreter,
 * each expecting a unique exit code back. If the waiter/response pairing
 * is ever crossed (wrong waiter signalled for a given response), a thread
 * will observe an exit code that belongs to a different thread and the
 * test fails deterministically.
 *
 * Design:
 *   - Single RubyVM, single RubyInterpreter.
 *   - N pthreads, each owns a unique non-zero exit code (10 + slot).
 *   - Each thread runs M sync iterations of `exit(MY_CODE)` and asserts
 *     execute_sync returns MY_CODE every time.
 *   - N*M total submissions racing through the same waiter list. With
 *     N=4 and M=25 that's 100 ordered request/response pairs whose
 *     pairing must hold across thread interleavings.
 *
 * If a regression flips the pop logic (e.g. always returns head regardless
 * of id, or uses LIFO), at least one thread will receive a code that
 * isn't its own and fail.
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

#define THREAD_COUNT 4
#define ITERATIONS_PER_THREAD 25
#define EXIT_CODE_BASE 10  /* slot 0 -> 10, slot 1 -> 11, ... — non-zero so we can
                              tell apart "got my code" from "got default 0". */

static RubyAPI g_ruby_api;

/* Print directly to pre-redirect terminal stderr — same trick as
 * test_multi_interpreter, so diag output survives logging shutdown. */
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

/* Suppress the SystemExit error spam each exit() call produces. Real test
 * failures show up via the per-thread mismatch counters, not Ruby logs. */
static void on_log(LogListener* listener, const char* line, log_stream_t stream, log_level_t level, int interpreter_id) {
    (void) listener;
    (void) stream;
    (void) level;
    (void) interpreter_id;
    /* Drop SystemExit chatter; surface everything else so a real Ruby
     * crash is still visible. */
    if (strstr(line, "SystemExit") != NULL) return;
    if (strstr(line, "exit intercepted") != NULL) return;
    if (strstr(line, "[SafeRunner]") != NULL) return;
    diag("[ruby] %s", line);
}

typedef struct {
    RubyInterpreter* interp;
    int slot;                  /* 0..THREAD_COUNT-1 */
    int expected_code;         /* EXIT_CODE_BASE + slot */
    int mismatch_count;        /* number of iterations where execute_sync != expected */
    int last_observed_code;    /* last wrong code observed, if any — for diag */
    int iterations_run;
} worker_args_t;

static void* worker(void* arg) {
    worker_args_t* w = (worker_args_t*)arg;
    char src[64];
    snprintf(src, sizeof(src), "exit(%d)\n", w->expected_code);

    for (int i = 0; i < ITERATIONS_PER_THREAD; i++) {
        RubyScript* s = g_ruby_api.script.create_from_content(src, strlen(src));
        if (s == NULL) {
            diag("worker[slot=%d] iter=%d: failed to allocate script", w->slot, i);
            w->mismatch_count++;
            continue;
        }
        int got = g_ruby_api.interpreter.execute_sync(w->interp, s);
        g_ruby_api.script.destroy(s);

        w->iterations_run++;
        if (got != w->expected_code) {
            w->mismatch_count++;
            w->last_observed_code = got;
            diag("worker[slot=%d] iter=%d: MISMATCH expected=%d got=%d",
                 w->slot, i, w->expected_code, got);
        }
    }
    return NULL;
}

int main(void) {
    int test_rc = 1;
    AssetsLayout* layout = NULL;
    AssetsError assets_error;
    RubyInterpreter* interp = NULL;

    assets_error_init(&assets_error);
    layout = assets_bootstrap("./test-ruby-install-same-id-fifo", &assets_error);
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

    interp = g_ruby_api.interpreter.create(".", layout->ruby_stdlib_path,
                                           layout->native_libs_dir, listener);
    if (interp == NULL) {
        diag("FAIL: interpreter.create");
        goto cleanup;
    }

    /* Warm up so the per-thread loop only sees steady-state cost. */
    {
        const char* warmup_src = "1 + 1\n";
        RubyScript* s = g_ruby_api.script.create_from_content(warmup_src, strlen(warmup_src));
        if (s == NULL) { diag("FAIL: warm-up alloc"); goto cleanup; }
        int rc = g_ruby_api.interpreter.execute_sync(interp, s);
        g_ruby_api.script.destroy(s);
        if (rc != 0) {
            diag("FAIL: warm-up returned %d", rc);
            goto cleanup;
        }
    }

    diag("DIAG: launching %d threads x %d iterations against one interpreter",
         THREAD_COUNT, ITERATIONS_PER_THREAD);

    worker_args_t args[THREAD_COUNT];
    pthread_t threads[THREAD_COUNT];

    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < THREAD_COUNT; i++) {
        args[i].interp = interp;
        args[i].slot = i;
        args[i].expected_code = EXIT_CODE_BASE + i;
        args[i].mismatch_count = 0;
        args[i].last_observed_code = -1;
        args[i].iterations_run = 0;
        int rc = pthread_create(&threads[i], NULL, worker, &args[i]);
        if (rc != 0) {
            diag("FAIL: pthread_create slot=%d (rc=%d)", i, rc);
            /* Best-effort: join already-started threads before bailing. */
            for (int j = 0; j < i; j++) pthread_join(threads[j], NULL);
            goto cleanup;
        }
    }

    for (int i = 0; i < THREAD_COUNT; i++) {
        pthread_join(threads[i], NULL);
    }

    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &end);
    long elapsed_ms = (end.tv_sec - start.tv_sec) * 1000L
                    + (end.tv_nsec - start.tv_nsec) / 1000000L;

    int total_mismatches = 0;
    int total_iterations = 0;
    for (int i = 0; i < THREAD_COUNT; i++) {
        total_mismatches += args[i].mismatch_count;
        total_iterations += args[i].iterations_run;
        diag("DIAG: slot=%d expected=%d iterations=%d mismatches=%d last_wrong=%d",
             args[i].slot, args[i].expected_code, args[i].iterations_run,
             args[i].mismatch_count, args[i].last_observed_code);
    }

    diag("DIAG: %d total iterations across %d threads in %ld ms (%d mismatches)",
         total_iterations, THREAD_COUNT, elapsed_ms, total_mismatches);

    if (total_iterations != THREAD_COUNT * ITERATIONS_PER_THREAD) {
        diag("FAIL: expected %d total iterations, got %d",
             THREAD_COUNT * ITERATIONS_PER_THREAD, total_iterations);
        goto cleanup;
    }

    if (total_mismatches != 0) {
        diag("FAIL: %d responses delivered to the wrong waiter — FIFO pairing broken",
             total_mismatches);
        goto cleanup;
    }

    diag("PASS: same-interpreter FIFO waiter pairing held across %d concurrent requests",
         total_iterations);
    test_rc = 0;

cleanup:
    if (interp != NULL) g_ruby_api.interpreter.destroy(interp);
    if (layout != NULL) assets_free_layout(layout);
    ruby_api_unload(&g_ruby_api);
    return test_rc;
}
