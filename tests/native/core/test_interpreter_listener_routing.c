/*
 * Per-interpreter LogListener routing regression test.
 *
 * Exercises the dispatch path's in-band-tag parser and the per-interpreter
 * listener registry without bringing up a full Ruby VM. Lines are written
 * directly into the logging-system pipes (the FDs Ruby workers would
 * normally write to via $stdout) and we assert that each registered
 * listener received exactly its own lines, with the right interpreter_id,
 * and that untagged lines (simulating native printf or untagged worker
 * writes) reach the first-registered listener as the natural fallback.
 *
 * Covers four cases:
 *   1. Tagged line routes to the listener registered for that id.
 *   2. Multiple interpreters coexist; each one only sees its own lines.
 *   3. Untagged lines (no \x01[…]\x01 prefix) reach the head of the
 *      registry (first-registered), tagged with LOG_NATIVE_INTERPRETER_ID.
 *   4. Malformed tags (truncated, non-numeric, missing closing brace) are
 *      treated as untagged — the line is delivered intact instead of
 *      dropped, and routes to the native fallback.
 */

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "embedded-ruby-vm/logging.h"
#include "embedded-ruby-vm/log-listener.h"

/* Diagnostic helper — see test_vmlogger_level.c. */
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

/* Each test listener owns a small ring of recorded lines. Mutex-protected
 * because the dispatch worker thread writes from a different thread than
 * the test's main thread reads. */
#define MAX_RECORDS_PER_LISTENER 16

typedef struct {
    char line[256];
    int  interpreter_id;
    log_stream_t source;
} record_t;

typedef struct {
    /* Tag the listener owns — used by the callback to know where to push. */
    int             owner_id;
    record_t        records[MAX_RECORDS_PER_LISTENER];
    int             count;
    pthread_mutex_t mutex;
} listener_state_t;

static void listener_state_init(listener_state_t* s, int owner_id) {
    s->owner_id = owner_id;
    s->count    = 0;
    pthread_mutex_init(&s->mutex, NULL);
}

static void listener_state_destroy(listener_state_t* s) {
    pthread_mutex_destroy(&s->mutex);
}

/* The on_log_message callback. The listener->user_data points back at
 * the listener_state_t the test set up; we append to its records ring. */
static void on_log(LogListener* listener,
                   const char* message,
                   log_stream_t source,
                   log_level_t level,
                   int interpreter_id) {
    (void)level;
    if (!listener || !listener->user_data || !message) return;
    listener_state_t* s = (listener_state_t*)listener->user_data;

    pthread_mutex_lock(&s->mutex);
    if (s->count < MAX_RECORDS_PER_LISTENER) {
        snprintf(s->records[s->count].line, sizeof(s->records[s->count].line),
                 "%s", message);
        s->records[s->count].interpreter_id = interpreter_id;
        s->records[s->count].source         = source;
        s->count++;
    }
    pthread_mutex_unlock(&s->mutex);
}

/* Locate a record whose payload equals `needle`. Returns NULL if not
 * found. Mutex held briefly during the linear scan. */
static const record_t* find_record(listener_state_t* s, const char* needle) {
    pthread_mutex_lock(&s->mutex);
    const record_t* found = NULL;
    for (int i = 0; i < s->count; i++) {
        if (strcmp(s->records[i].line, needle) == 0) {
            found = &s->records[i];
            break;
        }
    }
    pthread_mutex_unlock(&s->mutex);
    return found;
}

static int listener_count(listener_state_t* s) {
    pthread_mutex_lock(&s->mutex);
    int c = s->count;
    pthread_mutex_unlock(&s->mutex);
    return c;
}

static int write_line(int fd, const char* s) {
    size_t len = strlen(s);
    ssize_t n = write(fd, s, len);
    return (n == (ssize_t)len) ? 0 : -1;
}

/* Compose a tag prefix into `out`. `out` must hold at least 16 bytes. */
static void format_tag_prefix(char* out, size_t cap, int id) {
    snprintf(out, cap, "\x01[%d]\x01", id);
}

int main(void) {
    int rc = 0;

    if (logging_init("test_interpreter_listener_routing") != 0) {
        diag("FAIL: logging_init");
        return 1;
    }

    /* Set up two listeners. Registration order is meaningful: the head of
     * the registry list is the natural fallback for untagged lines, so
     * listener A (id=101) MUST be the one that receives the native /
     * malformed-tag lines below. */
    listener_state_t state_a, state_b;
    listener_state_init(&state_a, 101);
    listener_state_init(&state_b, 202);

    LogListener listener_a;
    log_listener_init(&listener_a);
    listener_a.user_data      = &state_a;
    listener_a.on_log_message = on_log;

    LogListener listener_b;
    log_listener_init(&listener_b);
    listener_b.user_data      = &state_b;
    listener_b.on_log_message = on_log;

    /* logging_register_interpreter_listener brings up the dispatch
     * thread on the FIRST registration (mirrors logging_add_custom_output's
     * behaviour for the legacy path) so consumers that only use the
     * registry don't need to also add a dummy custom_output to kick off
     * the worker. */
    if (logging_register_interpreter_listener(101, listener_a) != 0) {
        diag("FAIL: register listener A (id=101)");
        rc = 1;
        goto cleanup;
    }
    if (logging_register_interpreter_listener(202, listener_b) != 0) {
        diag("FAIL: register listener B (id=202)");
        rc = 1;
        goto cleanup;
    }

    int stdout_fd = logging_get_stream_fd(LOG_STREAM_RUBY_STDOUT);
    if (stdout_fd < 0) {
        diag("FAIL: logging_get_stream_fd(RUBY_STDOUT) returned %d", stdout_fd);
        rc = 1;
        goto cleanup;
    }

    /* --- Case 1 & 2: tagged lines route to the matching listener. --- */
    char a_line[256];
    char b_line[256];
    char prefix_a[16];
    char prefix_b[16];
    format_tag_prefix(prefix_a, sizeof(prefix_a), 101);
    format_tag_prefix(prefix_b, sizeof(prefix_b), 202);
    snprintf(a_line, sizeof(a_line), "%shello-from-A\n", prefix_a);
    snprintf(b_line, sizeof(b_line), "%shello-from-B\n", prefix_b);

    if (write_line(stdout_fd, a_line) != 0 || write_line(stdout_fd, b_line) != 0) {
        diag("FAIL: writing tagged lines to stdout_fd");
        rc = 1;
        goto cleanup;
    }

    /* --- Case 3: untagged native line falls back to listener A (head). --- */
    if (write_line(stdout_fd, "native-untagged-line\n") != 0) {
        diag("FAIL: writing untagged line");
        rc = 1;
        goto cleanup;
    }

    /* --- Case 4: malformed tags treated as untagged. ----------------- */
    /* Each of these is missing a piece the parser requires — by spec,
     * none should be misinterpreted as id=42, and the body should reach
     * the consumer intact (so post-mortem of mangled tags stays
     * possible). All route to the fallback (listener A). */
    if (write_line(stdout_fd, "\x01[42 truncated-no-close\n") != 0 ||           /* no ] */
        write_line(stdout_fd, "\x01[notnumeric]\x01nope-still-passes\n") != 0 || /* non-digit id */
        write_line(stdout_fd, "\x01[]\x01empty-id-passes\n") != 0) {              /* zero digits */
        diag("FAIL: writing malformed-tag lines");
        rc = 1;
        goto cleanup;
    }

    /* Drain so every line above has been dispatched before we inspect
     * the recorded ring. 2s matches test_vmlogger_level. */
    if (logging_drain(2000) != 0) {
        diag("FAIL: logging_drain timed out (dispatch thread not running, or queue stuck)");
        rc = 1;
        goto cleanup;
    }

    /* --- Assertions ---------------------------------------------------- */
    const record_t* r;

    /* Listener A must receive its own line. */
    r = find_record(&state_a, "hello-from-A");
    if (r == NULL) {
        diag("FAIL: listener A did not receive its tagged line");
        rc = 1;
    } else if (r->interpreter_id != 101) {
        diag("FAIL: listener A line carried interpreter_id=%d, expected 101", r->interpreter_id);
        rc = 1;
    }

    /* Listener A must NOT receive B's line (cross-routing leak). */
    if (find_record(&state_a, "hello-from-B") != NULL) {
        diag("FAIL: listener A received a line tagged for listener B — routing leak");
        rc = 1;
    }

    /* Listener B must receive its own line. */
    r = find_record(&state_b, "hello-from-B");
    if (r == NULL) {
        diag("FAIL: listener B did not receive its tagged line");
        rc = 1;
    } else if (r->interpreter_id != 202) {
        diag("FAIL: listener B line carried interpreter_id=%d, expected 202", r->interpreter_id);
        rc = 1;
    }

    /* Listener B must NOT receive A's line. */
    if (find_record(&state_b, "hello-from-A") != NULL) {
        diag("FAIL: listener B received a line tagged for listener A — routing leak");
        rc = 1;
    }

    /* Untagged native line → fallback (head = listener A). */
    r = find_record(&state_a, "native-untagged-line");
    if (r == NULL) {
        diag("FAIL: native (untagged) line did not reach fallback listener A");
        rc = 1;
    } else if (r->interpreter_id != LOG_NATIVE_INTERPRETER_ID) {
        diag("FAIL: native line carried interpreter_id=%d, expected LOG_NATIVE_INTERPRETER_ID (%d)",
             r->interpreter_id, LOG_NATIVE_INTERPRETER_ID);
        rc = 1;
    }
    if (find_record(&state_b, "native-untagged-line") != NULL) {
        diag("FAIL: native line ALSO reached listener B — should be head-only");
        rc = 1;
    }

    /* Malformed tags: bodies should arrive intact at listener A as native. */
    const char* malformed_bodies[] = {
        "\x01[42 truncated-no-close",      /* whole line passed through */
        "\x01[notnumeric]\x01nope-still-passes",
        "\x01[]\x01empty-id-passes",
    };
    for (size_t i = 0; i < sizeof(malformed_bodies) / sizeof(malformed_bodies[0]); i++) {
        r = find_record(&state_a, malformed_bodies[i]);
        if (r == NULL) {
            diag("FAIL: malformed-tag line missing from listener A: %s", malformed_bodies[i]);
            rc = 1;
        } else if (r->interpreter_id != LOG_NATIVE_INTERPRETER_ID) {
            diag("FAIL: malformed-tag line %s routed as id=%d, expected NATIVE",
                 malformed_bodies[i], r->interpreter_id);
            rc = 1;
        }
    }

    /* --- Unregister + verify subsequent lines stop reaching A. ------- */
    if (logging_unregister_interpreter_listener(101) != 0) {
        diag("FAIL: unregister listener A");
        rc = 1;
        goto cleanup;
    }
    int count_a_before = listener_count(&state_a);
    /* After A is unregistered, the fallback HEAD becomes listener B.
     * A's record ring must NOT grow on subsequent untagged writes; B's
     * MUST grow once. */
    if (write_line(stdout_fd, "post-unregister-native\n") != 0) {
        diag("FAIL: writing post-unregister line");
        rc = 1;
        goto cleanup;
    }
    if (logging_drain(2000) != 0) {
        diag("FAIL: logging_drain after unregister");
        rc = 1;
        goto cleanup;
    }
    int count_a_after = listener_count(&state_a);
    if (count_a_after != count_a_before) {
        diag("FAIL: listener A received a line after unregister (count went %d -> %d)",
             count_a_before, count_a_after);
        rc = 1;
    }
    r = find_record(&state_b, "post-unregister-native");
    if (r == NULL) {
        diag("FAIL: post-unregister native line did not fall back to listener B (new head)");
        rc = 1;
    }

cleanup:
    /* Defensive: tear everything down even if the test failed mid-way. */
    (void) logging_unregister_interpreter_listener(101);
    (void) logging_unregister_interpreter_listener(202);
    listener_state_destroy(&state_a);
    listener_state_destroy(&state_b);
    logging_shutdown();

    if (rc != 0) {
        diag("FAIL: per-interpreter routing regressed");
        return 1;
    }
    diag("PASS: tagged routing + native fallback + unregister behaviour all correct");
    return 0;
}
