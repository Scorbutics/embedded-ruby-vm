/*
 * Host event dispatch test (VMHost.emit, the C side).
 *
 * Writes event lines, as vm_host.rb does, straight into the vmlogger pipe,
 * without a Ruby VM, and checks:
 *   1. An event reaches the LogListener of its interpreter only, as
 *      LOG_STREAM_HOST_EVENT "<topic> <base64>", and no custom output.
 *   2. An untagged event (a Thread no interpreter owns) and an event of an
 *      interpreter without a listener reach no listener (not the oldest).
 *   3. PART lines are joined into one event, even with a drain marker
 *      between them; an event cut short by a line of another event is
 *      dropped, that other event still arrives.
 *   4. Events are not dropped: 1000 events to a slow listener (four times
 *      the queue's bound) all arrive, in order.
 *   5. A host event callback runs outside the logging lock: it can register
 *      another listener; while it runs, a timed drain times out (its late
 *      callback is then safe), and re-registering its interpreter waits for
 *      it to end, as unregistering does.
 *   6. Concurrent drains (their markers in any order) all complete.
 */

#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "embedded-ruby-vm/logging.h"
#include "embedded-ruby-vm/log-listener.h"

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

#define MAX_EVENTS 1100

typedef struct {
    char* events[MAX_EVENTS];   /* the messages of LOG_STREAM_HOST_EVENT */
    atomic_int count;
} listener_state_t;

static listener_state_t g_a, g_b;
static atomic_int g_events_elsewhere = 0;   /* events seen by a custom output */
static atomic_int g_slow_events = 0;
static int g_custom_context = 1;

static void on_log(LogListener* listener, const char* message, log_stream_t source,
                   log_level_t level, int interpreter_id) {
    (void)level; (void)interpreter_id;
    listener_state_t* s = (listener_state_t*)listener->user_data;
    if (source == LOG_STREAM_HOST_EVENT) {
        int index = atomic_load(&s->count);
        if (index < MAX_EVENTS) s->events[index] = strdup(message);
        atomic_store(&s->count, index + 1);
        if (atomic_load(&g_slow_events)) usleep(200);
    }
}

/* Case 5: the listener of interpreter 11 */
static atomic_int g_pinned_started = 0, g_pinned_ended = 0, g_pinned_calls = 0, g_registered_inside = -1;
static atomic_int g_pinned_release = 0;

static void on_pinned(LogListener* listener, const char* message, log_stream_t source,
                      log_level_t level, int interpreter_id) {
    (void)message; (void)level; (void)interpreter_id;
    if (source != LOG_STREAM_HOST_EVENT) return;
    atomic_fetch_add(&g_pinned_calls, 1);
    atomic_store(&g_pinned_started, 1);
    LogListener other;
    log_listener_init(&other);
    other.on_log_message = on_log;
    other.user_data = &g_b;
    atomic_store(&g_registered_inside, logging_register_interpreter_listener(12, other));
    for (int i = 0; i < 500 && !atomic_load(&g_pinned_release); i++) usleep(10 * 1000);
    atomic_store(&g_pinned_ended, 1);
}

static void* release_pinned_later(void* unused) {
    (void)unused;
    usleep(200 * 1000);
    atomic_store(&g_pinned_release, 1);
    return NULL;
}

static void* drain_repeatedly(void* result) {
    for (int i = 0; i < 50; i++) {
        if (logging_drain(5000) != 0) {
            *(int*)result = 1;
            break;
        }
    }
    return NULL;
}

static int on_custom_output(const char* line, log_stream_t stream, log_level_t level, void* context) {
    (void)level; (void)context;
    if (stream == LOG_STREAM_HOST_EVENT || strstr(line, "VMHOST") != NULL) atomic_fetch_add(&g_events_elsewhere, 1);
    return 0;
}

/* Wait until `s` holds `expected` events (15 s at most). */
static int wait_events(listener_state_t* s, int expected) {
    for (int i = 0; i < 1500; i++) {
        if (atomic_load(&s->count) >= expected) return 1;
        usleep(10 * 1000);
    }
    return 0;
}

static int write_str(int fd, const char* s) {
    size_t len = strlen(s);
    return write(fd, s, len) == (ssize_t)len ? 0 : -1;
}

static int check_event(listener_state_t* s, int index, const char* expected) {
    if (atomic_load(&s->count) <= index || strcmp(s->events[index], expected) != 0) {
        diag("FAIL: event %d is '%s', expected '%s'", index,
             atomic_load(&s->count) > index ? s->events[index] : "(none)", expected);
        return 0;
    }
    return 1;
}

int main(void) {
    int rc = 1;
    if (logging_init("test_host_events") != 0) {
        diag("FAIL: logging_init");
        return 1;
    }
    LogListener listener_a, listener_b;
    log_listener_init(&listener_a);
    listener_a.user_data = &g_a;
    listener_a.on_log_message = on_log;
    log_listener_init(&listener_b);
    listener_b.user_data = &g_b;
    listener_b.on_log_message = on_log;
    /* A (7) first: the fallback of untagged log lines, never of events */
    if (logging_register_interpreter_listener(7, listener_a) != 0 ||
        logging_register_interpreter_listener(9, listener_b) != 0 ||
        logging_add_custom_output(on_custom_output, &g_custom_context) != 0) {
        diag("FAIL: listener registration");
        goto cleanup;
    }
    int fd = logging_get_stream_fd(LOG_STREAM_VMLOGGER);
    if (fd < 0) {
        diag("FAIL: no vmlogger fd");
        goto cleanup;
    }

    /* Cases 1 and 2. "\x01" is followed by a separate literal: a hex escape
     * would swallow the letters after it. */
    if (write_str(fd, "<<<VMHOST:EVENT>>> \x01[9]\x01" "overlay.edited eyJ4IjoxfQ==\n") != 0 ||
        write_str(fd, "<<<VMHOST:EVENT>>> untagged.topic eA==\n") != 0 ||
        write_str(fd, "<<<VMHOST:EVENT>>> \x01[42]\x01" "no.listener eA==\n") != 0 ||
        write_str(fd, "<<<VMHOST:EVENT>>> \x01[7]\x01" "game.ready \n") != 0) {
        diag("FAIL: writing events");
        goto cleanup;
    }
    if (!wait_events(&g_a, 1) || !wait_events(&g_b, 1)) {
        diag("FAIL: events not received (a=%d b=%d)", atomic_load(&g_a.count), atomic_load(&g_b.count));
        goto cleanup;
    }
    /* Dispatch is FIFO: game.ready came last, the lines before it are done */
    if (!check_event(&g_b, 0, "overlay.edited eyJ4IjoxfQ==") || !check_event(&g_a, 0, "game.ready ") ||
        atomic_load(&g_a.count) != 1 || atomic_load(&g_b.count) != 1) {
        diag("FAIL: an event reached another interpreter (a=%d b=%d)", atomic_load(&g_a.count), atomic_load(&g_b.count));
        goto cleanup;
    }

    /* Case 3 */
    if (write_str(fd, "<<<VMHOST:PART>>> \x01[7]\x01" "big.event AAAA\n") != 0 ||
        logging_drain(5000) != 0 ||
        write_str(fd, "<<<VMHOST:PART>>> \x01[7]\x01" "big.event BBBB\n") != 0 ||
        write_str(fd, "<<<VMHOST:EVENT>>> \x01[7]\x01" "big.event CC==\n") != 0 ||
        write_str(fd, "<<<VMHOST:PART>>> \x01[7]\x01" "cut.short AAAA\n") != 0 ||
        write_str(fd, "<<<VMHOST:EVENT>>> \x01[7]\x01" "next.event eA==\n") != 0) {
        diag("FAIL: writing parts");
        goto cleanup;
    }
    if (!wait_events(&g_a, 3) || !check_event(&g_a, 1, "big.event AAAABBBBCC==") ||
        !check_event(&g_a, 2, "next.event eA==")) {
        goto cleanup;
    }

    /* Case 4 */
    atomic_store(&g_slow_events, 1);
    char line[128];
    for (int i = 0; i < 1000; i++) {
        snprintf(line, sizeof(line), "<<<VMHOST:EVENT>>> \x01[7]\x01" "burst.%d eA==\n", i);
        if (write_str(fd, line) != 0) {
            diag("FAIL: writing burst event %d", i);
            goto cleanup;
        }
    }
    if (!wait_events(&g_a, 1003)) {
        diag("FAIL: burst events dropped (a=%d, expected 1003)", atomic_load(&g_a.count));
        goto cleanup;
    }
    atomic_store(&g_slow_events, 0);
    for (int i = 0; i < 1000; i++) {
        snprintf(line, sizeof(line), "burst.%d eA==", i);
        if (!check_event(&g_a, 3 + i, line)) goto cleanup;
    }

    /* Case 5 */
    LogListener pinned;
    log_listener_init(&pinned);
    pinned.on_log_message = on_pinned;
    if (logging_register_interpreter_listener(11, pinned) != 0 ||
        write_str(fd, "<<<VMHOST:EVENT>>> \x01[11]\x01" "pin.first eA==\n") != 0) {
        diag("FAIL: case 5 setup");
        goto cleanup;
    }
    for (int i = 0; i < 500 && !atomic_load(&g_pinned_started); i++) usleep(10 * 1000);
    if (!atomic_load(&g_pinned_started)) {
        diag("FAIL: case 5 callback not called");
        goto cleanup;
    }
    if (logging_drain(50) != -1) {
        diag("FAIL: a drain did not time out behind a running callback");
        goto cleanup;
    }
    /* Re-registered: the next event goes to the new listener (g_b) */
    LogListener replacement;
    log_listener_init(&replacement);
    replacement.on_log_message = on_log;
    replacement.user_data = &g_b;
    int b_before = atomic_load(&g_b.count);
    pthread_t releaser;
    if (pthread_create(&releaser, NULL, release_pinned_later, NULL) != 0) {
        diag("FAIL: case 5 thread");
        goto cleanup;
    }
    int replaced = logging_register_interpreter_listener(11, replacement);
    pthread_join(releaser, NULL);
    if (replaced != 0 || !atomic_load(&g_pinned_ended)) {
        diag("FAIL: re-register (rc=%d) returned while the previous listener's callback ran", replaced);
        goto cleanup;
    }
    if (write_str(fd, "<<<VMHOST:EVENT>>> \x01[11]\x01" "pin.second eA==\n") != 0 ||
        !wait_events(&g_b, b_before + 1) || !check_event(&g_b, b_before, "pin.second eA==")) {
        goto cleanup;
    }
    logging_unregister_interpreter_listener(11);
    if (logging_drain(5000) != 0 || atomic_load(&g_pinned_calls) != 1 || atomic_load(&g_registered_inside) != 0) {
        diag("FAIL: case 5 (calls=%d, registered inside=%d)",
             atomic_load(&g_pinned_calls), atomic_load(&g_registered_inside));
        goto cleanup;
    }

    /* Case 6 */
    pthread_t drainers[4];
    int drain_failed[4] = { 0, 0, 0, 0 };
    int started = 0;
    while (started < 4 && pthread_create(&drainers[started], NULL, drain_repeatedly, &drain_failed[started]) == 0) started++;
    for (int i = 0; i < started; i++) pthread_join(drainers[i], NULL);
    if (started < 4) {
        diag("FAIL: case 6 threads");
        goto cleanup;
    }
    if (drain_failed[0] || drain_failed[1] || drain_failed[2] || drain_failed[3]) {
        diag("FAIL: a concurrent drain did not complete");
        goto cleanup;
    }

    if (atomic_load(&g_events_elsewhere) != 0) {
        diag("FAIL: %d events reached a custom output", atomic_load(&g_events_elsewhere));
        goto cleanup;
    }

    diag("PASS: test_host_events");
    rc = 0;

cleanup:
    atomic_store(&g_slow_events, 0);
    atomic_store(&g_pinned_release, 1);
    logging_remove_custom_output(on_custom_output, &g_custom_context);
    logging_unregister_interpreter_listener(7);
    logging_unregister_interpreter_listener(9);
    logging_unregister_interpreter_listener(11);
    logging_unregister_interpreter_listener(12);
    logging_shutdown();
    return rc;
}
