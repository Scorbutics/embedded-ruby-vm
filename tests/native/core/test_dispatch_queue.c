/*
 * Tests for the async dispatch queue introduced when removing the dup2-swap
 * race in logging.c.
 *
 * Three independent test cases:
 *   1. test_basic_dispatch:
 *      A single line written to RUBY_STDOUT must reach a registered custom
 *      callback exactly once. Smoke test that the worker thread is alive,
 *      the queue dispatches in FIFO order, and logging_drain() works.
 *
 *   2. test_flood_with_drops:
 *      Burst-write thousands of lines while the callback is intentionally
 *      slow (usleep). The queue is bounded; the system must NOT hang or
 *      crash when the queue fills, and a non-trivial number of lines must
 *      still reach the callback. Drops are expected — they're better than
 *      unbounded growth.
 *
 *   3. test_callback_no_deadlock:
 *      Register a callback that violates the contract by writing to fd 1
 *      (the redirected stdout). This produces another LOG_LINE that triggers
 *      the same callback, which would feedback-loop forever in a synchronous
 *      design or deadlock if the logger thread were holding the pipe open
 *      under itself. The async-dispatch design must survive this case: the
 *      dispatcher continues draining the pipe regardless, the callback gets
 *      invoked some bounded number of times, and shutdown completes
 *      cleanly. The callback caps its own recursion at a small N so the test
 *      eventually quiesces.
 *
 * The tests do NOT load the Ruby VM — they exercise logging.c directly via
 * its private API and write to LOG_STREAM_RUBY_STDOUT through
 * logging_get_stream_fd(). That keeps each test fast and isolated from
 * Ruby-side state.
 */

#include <errno.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "embedded-ruby-vm/logging.h"
#include "embedded-ruby-vm/log-listener.h"

/* Use a non-NULL sentinel for context — logging.c skips callback nodes whose
 * context is NULL (a long-standing quirk that's outside the scope of this
 * test file). */
static int g_callback_context = 1;

static atomic_int g_invocation_count;
static atomic_int g_recursive_print_count;

static long elapsed_ms(struct timespec start) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - start.tv_sec) * 1000L
         + (now.tv_nsec - start.tv_nsec) / 1000000L;
}

/* Print directly to the pre-redirect terminal stderr so diagnostic output is
 * visible even though logging is redirecting fd 2 during these tests. Same
 * trick test_sentinel_log_level uses. */
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

/* ----- callbacks ---------------------------------------------------------- */

static int counter_callback(const char* line, log_stream_t stream, void* context) {
    (void)line; (void)stream; (void)context;
    atomic_fetch_add(&g_invocation_count, 1);
    return 0;
}

static int slow_counter_callback(const char* line, log_stream_t stream, void* context) {
    (void)line; (void)stream; (void)context;
    atomic_fetch_add(&g_invocation_count, 1);
    /* Slow consumer: forces the producer (logger thread) to outpace the
     * worker, so the dispatch queue actually fills and drops trigger. */
    usleep(200);
    return 0;
}

#define RECURSIVE_PRINT_CAP 5

static int recursive_printer_callback(const char* line, log_stream_t stream, void* context) {
    (void)line; (void)stream; (void)context;
    int prev = atomic_fetch_add(&g_invocation_count, 1);
    /* Up to N invocations write back to fd 1 (the redirected stdout = native
     * stdout pipe). Each write produces another line that the logger reads
     * and re-dispatches to this same callback — the contract violation we
     * documented. After the cap, stop printing so the recursion eventually
     * runs out of fuel and the test can shut down cleanly. */
    if (prev < RECURSIVE_PRINT_CAP) {
        atomic_fetch_add(&g_recursive_print_count, 1);
        static const char msg[] = "recursive!\n";
        ssize_t ignored = write(STDOUT_FILENO, msg, sizeof(msg) - 1);
        (void)ignored;
    }
    return 0;
}

/* ----- test cases --------------------------------------------------------- */

/* Spin-wait helper: poll an atomic counter until it reaches `target` or the
 * timeout expires. Returns the final observed value. */
static int wait_for_count_at_least(atomic_int* counter, int target, long timeout_ms) {
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    int v;
    while ((v = atomic_load(counter)) < target) {
        if (elapsed_ms(start) > timeout_ms) break;
        usleep(2000);
    }
    return v;
}

static int test_basic_dispatch(void) {
    diag("=== test_basic_dispatch ===");
    atomic_store(&g_invocation_count, 0);

    if (logging_init("test_dispatch") != 0) {
        diag("FAIL: logging_init");
        return 1;
    }
    if (logging_add_custom_output(counter_callback, &g_callback_context) != 0) {
        diag("FAIL: logging_add_custom_output");
        logging_shutdown();
        return 1;
    }

    int fd = logging_get_stream_fd(LOG_STREAM_RUBY_STDOUT);
    if (fd < 0) {
        diag("FAIL: logging_get_stream_fd returned %d", fd);
        logging_remove_custom_output(counter_callback, &g_callback_context);
        logging_shutdown();
        return 1;
    }

    static const char msg[] = "hello dispatch\n";
    if (write(fd, msg, sizeof(msg) - 1) != (ssize_t)(sizeof(msg) - 1)) {
        diag("FAIL: write to stream FD: %s", strerror(errno));
        logging_remove_custom_output(counter_callback, &g_callback_context);
        logging_shutdown();
        return 1;
    }

    int drain_rc = logging_drain(2000);
    int count = atomic_load(&g_invocation_count);

    logging_remove_custom_output(counter_callback, &g_callback_context);
    logging_shutdown();

    if (drain_rc != 0) {
        diag("FAIL: logging_drain returned %d (timeout?)", drain_rc);
        return 1;
    }
    if (count != 1) {
        diag("FAIL: expected exactly 1 callback invocation, got %d", count);
        return 1;
    }
    diag("PASS: basic dispatch");
    return 0;
}

static int test_flood_with_drops(void) {
    diag("=== test_flood_with_drops ===");
    atomic_store(&g_invocation_count, 0);

    if (logging_init("test_dispatch") != 0) {
        diag("FAIL: logging_init");
        return 1;
    }
    if (logging_add_custom_output(slow_counter_callback, &g_callback_context) != 0) {
        diag("FAIL: logging_add_custom_output");
        logging_shutdown();
        return 1;
    }

    int fd = logging_get_stream_fd(LOG_STREAM_RUBY_STDOUT);
    if (fd < 0) {
        diag("FAIL: logging_get_stream_fd returned %d", fd);
        logging_remove_custom_output(slow_counter_callback, &g_callback_context);
        logging_shutdown();
        return 1;
    }

    /* Burst-write many short lines. With a 200us-per-callback consumer and
     * a 1024-cap queue, the queue should fill and drops should trigger. */
    const int sent = 5000;
    static const char line[] = "x\n";
    int written_lines = 0;
    for (int i = 0; i < sent; i++) {
        ssize_t n = write(fd, line, sizeof(line) - 1);
        if (n != (ssize_t)(sizeof(line) - 1)) {
            diag("write returned %zd at i=%d errno=%d (%s) — stopping early",
                 n, i, errno, strerror(errno));
            break;
        }
        written_lines++;
    }

    /* Generous timeout: ~1024 items at 200us ≈ 200ms, plus pipe drainage. */
    int drain_rc = logging_drain(30000);
    int received = atomic_load(&g_invocation_count);

    logging_remove_custom_output(slow_counter_callback, &g_callback_context);
    logging_shutdown();

    if (drain_rc != 0) {
        diag("FAIL: logging_drain returned %d (hung?)", drain_rc);
        return 1;
    }

    int dropped = written_lines - received;
    diag("Sent %d, received %d, dropped %d (%.1f%%)",
         written_lines, received, dropped,
         written_lines > 0 ? (100.0 * dropped) / written_lines : 0.0);

    if (received < 1) {
        diag("FAIL: zero callbacks fired despite %d writes", written_lines);
        return 1;
    }
    if (received > written_lines) {
        diag("FAIL: callback fired %d times for %d writes — phantom items?",
             received, written_lines);
        return 1;
    }
    /* Drops aren't strictly required (a fast machine may drain in step), but
     * the system must not hang and at least some lines must arrive. */
    diag("PASS: flood survived without hang or crash");
    return 0;
}

static int test_callback_no_deadlock(void) {
    diag("=== test_callback_no_deadlock ===");
    atomic_store(&g_invocation_count, 0);
    atomic_store(&g_recursive_print_count, 0);

    if (logging_init("test_dispatch") != 0) {
        diag("FAIL: logging_init");
        return 1;
    }
    if (logging_add_custom_output(recursive_printer_callback, &g_callback_context) != 0) {
        diag("FAIL: logging_add_custom_output");
        logging_shutdown();
        return 1;
    }

    int fd = logging_get_stream_fd(LOG_STREAM_RUBY_STDOUT);
    if (fd < 0) {
        diag("FAIL: logging_get_stream_fd returned %d", fd);
        logging_remove_custom_output(recursive_printer_callback, &g_callback_context);
        logging_shutdown();
        return 1;
    }

    /* One trigger line. The callback's printf to fd 1 generates a line on
     * NATIVE_STDOUT, which the logger reads, dispatches to the same callback,
     * which prints again, ... up to RECURSIVE_PRINT_CAP times. */
    static const char msg[] = "trigger\n";
    if (write(fd, msg, sizeof(msg) - 1) != (ssize_t)(sizeof(msg) - 1)) {
        diag("FAIL: write to stream FD: %s", strerror(errno));
        logging_remove_custom_output(recursive_printer_callback, &g_callback_context);
        logging_shutdown();
        return 1;
    }

    /* Wait until the callback has been invoked at least RECURSIVE_PRINT_CAP+1
     * times (initial + 5 prints' echoes) or 5s passes. logging_drain() is
     * unreliable here because the callback enqueues new items WHILE the
     * drain barrier is being processed, so the barrier can fire before all
     * recursive items are dispatched. Polling the counter is the right
     * primitive for "did the recursive chain actually run". */
    int observed = wait_for_count_at_least(&g_invocation_count,
                                           RECURSIVE_PRINT_CAP + 1, 5000);

    /* Then explicitly drain anything still pending so shutdown is clean. */
    int drain_rc = logging_drain(5000);
    int total = atomic_load(&g_invocation_count);
    int prints = atomic_load(&g_recursive_print_count);

    logging_remove_custom_output(recursive_printer_callback, &g_callback_context);
    logging_shutdown();

    if (drain_rc != 0) {
        diag("FAIL: logging_drain returned %d after recursive workload — possible deadlock",
             drain_rc);
        return 1;
    }

    diag("invocations=%d (waited until >= %d, observed %d), recursive prints=%d",
         total, RECURSIVE_PRINT_CAP + 1, observed, prints);

    /* The recursive chain must have actually run — at least one feedback
     * iteration on top of the initial trigger. */
    if (total < 2) {
        diag("FAIL: callback fired only %d times — recursion didn't propagate", total);
        return 1;
    }
    if (prints < 1) {
        diag("FAIL: callback never printed back to stdout — test setup broken");
        return 1;
    }
    /* The cap on prints prevents truly unbounded growth; if total is wildly
     * larger than expected, something kept producing lines and we'd see
     * runaway numbers. The bound is loose because the queue may also drop
     * lines under contention. */
    if (total > 10 * (RECURSIVE_PRINT_CAP + 1)) {
        diag("WARN: callback fired %d times — much higher than expected ceiling",
             total);
    }
    diag("PASS: recursive-printing callback survived without deadlock");
    return 0;
}

int main(void) {
    int rc = 0;
    rc |= test_basic_dispatch();
    rc |= test_flood_with_drops();
    rc |= test_callback_no_deadlock();
    if (rc == 0) {
        diag("=== all dispatch-queue tests PASSED ===");
    } else {
        diag("=== one or more dispatch-queue tests FAILED ===");
    }
    return rc;
}
