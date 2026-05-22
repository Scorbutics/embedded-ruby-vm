/*
 * Order-tolerance regression test for the per-interpreter listener registry.
 *
 * The Android consumer (PsdkInterpreter) registers its log listener at
 * KmpInterpreter.create() time — which calls into ruby_interpreter_create
 * → logging_register_interpreter_listener — and ONLY then calls
 * enableLogging(), which is what eventually invokes logging_init via
 * ruby_vm_enable_logging.
 *
 * Before the fix this exercised, the first register call would try to
 * bring up the dispatch thread (since interpreter_listener_count just
 * went from 0 to 1 and no custom_outputs existed yet), see
 * is_initialized==false inside internal_start_logging_thread, return
 * LOGGING_ERROR_NOT_INITIALIZED, and the calling code would roll back
 * the registration. The consumer's listener was silently dropped — every
 * subsequent log line bypassed onLogMessage and the consumer's loader
 * UX (overlay fade gated on [loader-ready]) would freeze on the opaque
 * overlay.
 *
 * The fix makes registration order-tolerant: when called before init,
 * the entry stays in the registry; the thread comes up later when
 * logging_init + logging_add_custom_output (or another register that
 * follows logging_init) fires.
 *
 * This test pins that contract end-to-end at the C surface.
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

/* Diagnostic helper — same pattern as test_interpreter_listener_routing. */
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

/* Minimal listener: counts calls, captures the last (body, interpreter_id)
 * pair under a mutex so the dispatch worker can race with the main thread
 * without UB. */
typedef struct {
    pthread_mutex_t lock;
    int count;
    char last_body[256];
    int last_interpreter_id;
} listener_state_t;

static void on_log(LogListener* listener, const char* line,
                   log_stream_t stream, log_level_t level,
                   int interpreter_id) {
    (void)stream;
    (void)level;
    listener_state_t* st = (listener_state_t*)listener->user_data;
    pthread_mutex_lock(&st->lock);
    st->count++;
    strncpy(st->last_body, line, sizeof(st->last_body) - 1);
    st->last_body[sizeof(st->last_body) - 1] = '\0';
    /* Strip the trailing newline so equality compares are clean. */
    size_t blen = strlen(st->last_body);
    if (blen > 0 && st->last_body[blen - 1] == '\n') {
        st->last_body[blen - 1] = '\0';
    }
    st->last_interpreter_id = interpreter_id;
    pthread_mutex_unlock(&st->lock);
}

/* Stand-in for what ruby_vm_enable_logging does end-to-end on the first
 * call: init the system and add a (no-op) custom_output that hosts the
 * dispatch thread. The thread is started by logging_add_custom_output on
 * its first add when custom_output_count goes 0→1. */
static int dummy_custom_output(const char* line, log_stream_t stream,
                               log_level_t level, void* context) {
    (void)line; (void)stream; (void)level; (void)context;
    return 0;
}

/* Write one line into the RUBY_STDOUT pipe — the same path Ruby workers
 * use through their TaggedIO wrapper. */
static int write_line(int fd, const char* line) {
    size_t len = strlen(line);
    ssize_t n = write(fd, line, len);
    return (n == (ssize_t)len) ? 0 : -1;
}

int main(void) {
    int rc = 0;
    listener_state_t state;
    memset(&state, 0, sizeof(state));
    pthread_mutex_init(&state.lock, NULL);

    LogListener listener;
    log_listener_init(&listener);
    listener.user_data      = &state;
    listener.on_log_message = on_log;

    /* --- Step 1: register BEFORE logging_init. --- */
    /* Pre-condition: logging_init has NOT been called. is_initialized=0.
     * The bug being pinned: this used to roll back the registration and
     * return non-zero. The fix is to register successfully (return 0)
     * and defer thread-start to whichever later call ends up initializing
     * the logging system. */
    int reg_rc = logging_register_interpreter_listener(/*interpreter_id*/ 42, listener);
    if (reg_rc != 0) {
        diag("FAIL: logging_register_interpreter_listener returned %d before init — "
             "registration must be order-tolerant", reg_rc);
        rc = 1;
        goto cleanup;
    }

    /* --- Step 2: simulate ruby_vm_enable_logging — init + first add. --- */
    /* This is what eventually fires inside ensure_vm_initialized when the
     * consumer's enableLogging() reaches the C side. logging_add_custom_output
     * starts the dispatch thread on its first add since the thread wasn't
     * brought up at register time. */
    if (logging_init("test_listener_register_before_init") != 0) {
        diag("FAIL: logging_init after register");
        rc = 1;
        goto cleanup;
    }
    if (logging_add_custom_output(dummy_custom_output, &state) != 0) {
        diag("FAIL: logging_add_custom_output failed to start the dispatch thread");
        rc = 1;
        goto cleanup;
    }

    /* --- Step 3: write a tagged line, drain, verify delivery. --- */
    int stdout_fd = logging_get_stream_fd(LOG_STREAM_RUBY_STDOUT);
    if (stdout_fd < 0) {
        diag("FAIL: logging_get_stream_fd(RUBY_STDOUT) returned %d after enable", stdout_fd);
        rc = 1;
        goto cleanup;
    }

    /* "\x01[42]\x01" matches the TaggedIO prefix the Ruby side emits when
     * the producing worker's Thread.current[THREAD_INTERP_KEY] is 42. */
    if (write_line(stdout_fd, "\x01[42]\x01hello-after-deferred-init\n") != 0) {
        diag("FAIL: writing tagged line to RUBY_STDOUT pipe");
        rc = 1;
        goto cleanup;
    }

    if (logging_drain(2000) != 0) {
        diag("FAIL: logging_drain timed out — dispatch thread not running, "
             "or the registry entry was dropped at register-time");
        rc = 1;
        goto cleanup;
    }

    pthread_mutex_lock(&state.lock);
    int count = state.count;
    int seen_id = state.last_interpreter_id;
    char body_copy[sizeof(state.last_body)];
    strncpy(body_copy, state.last_body, sizeof(body_copy));
    body_copy[sizeof(body_copy) - 1] = '\0';
    pthread_mutex_unlock(&state.lock);

    if (count != 1) {
        diag("FAIL: listener received %d lines, expected 1 — register-before-init must keep the entry live", count);
        rc = 1;
        goto cleanup;
    }
    if (strcmp(body_copy, "hello-after-deferred-init") != 0) {
        diag("FAIL: listener body=%s, expected 'hello-after-deferred-init' (tag should be stripped)", body_copy);
        rc = 1;
        goto cleanup;
    }
    if (seen_id != 42) {
        diag("FAIL: listener interpreter_id=%d, expected 42", seen_id);
        rc = 1;
        goto cleanup;
    }

    diag("PASS: registration before logging_init is order-tolerant; "
         "thread comes up on the deferred add, line routed to id=42 listener");

cleanup:
    (void) logging_unregister_interpreter_listener(42);
    (void) logging_remove_custom_output(dummy_custom_output, &state);
    logging_shutdown();
    pthread_mutex_destroy(&state.lock);

    return rc;
}
