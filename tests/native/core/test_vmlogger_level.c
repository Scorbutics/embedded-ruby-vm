/*
 * VMLogger severity propagation regression test.
 *
 * fifo_interpreter.rb funnels VMLogger.debug / VMLogger.info / VMLogger.error
 * all into the single vmlogger pipe (LOG_STREAM_VMLOGGER), which previously
 * flattened the per-call severity into a single LogSource.VMLOGGER on the host.
 * safe_runner.rb now prefixes every line with a `<<<VMLOG:LEVEL>>> ` tag
 * (constants.h: VMLOGGER_LEVEL_TAG_*), and the C logging system strips the
 * tag in write_full_log_line and surfaces the parsed severity as
 * log_level_t on the custom-output callback.
 *
 * This test exercises that path directly — no Ruby VM involved — by writing
 * tagged and untagged bytes into the VMLOGGER pipe and asserting the callback
 * sees the right (line, stream, level) tuple. It also covers default-level
 * derivation for non-VMLOGGER streams (RUBY_STDOUT -> INFO, RUBY_STDERR ->
 * ERROR), which lives in the same code path.
 */

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "embedded-ruby-vm/logging.h"
#include "embedded-ruby-vm/log-listener.h"
#include "embedded-ruby-vm/constants.h"

/* Diagnostic helper — see test_dispatch_queue.c for the same trick. */
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

/* Recorded log lines — mutex-protected so we can safely append from the
 * dispatch worker thread and read from the test thread after the drain. */
#define MAX_RECORDS 16

typedef struct {
    char         line[256];
    log_stream_t stream;
    log_level_t  level;
} record_t;

static record_t        g_records[MAX_RECORDS];
static int             g_record_count = 0;
static pthread_mutex_t g_records_mutex = PTHREAD_MUTEX_INITIALIZER;
static int             g_callback_context = 1;  /* non-NULL sentinel */

static int record_callback(const char* line, log_stream_t stream, log_level_t level, void* context) {
    (void)context;
    pthread_mutex_lock(&g_records_mutex);
    if (g_record_count < MAX_RECORDS) {
        snprintf(g_records[g_record_count].line, sizeof(g_records[g_record_count].line), "%s", line);
        g_records[g_record_count].stream = stream;
        g_records[g_record_count].level  = level;
        g_record_count++;
    }
    pthread_mutex_unlock(&g_records_mutex);
    return 0;
}

/* Find a recorded entry whose line equals `needle`. Returns the entry or
 * NULL. Locks the records mutex internally. */
static const record_t* find_record(const char* needle) {
    pthread_mutex_lock(&g_records_mutex);
    const record_t* found = NULL;
    for (int i = 0; i < g_record_count; i++) {
        if (strcmp(g_records[i].line, needle) == 0) {
            found = &g_records[i];
            break;
        }
    }
    pthread_mutex_unlock(&g_records_mutex);
    return found;
}

static int write_line(int fd, const char* s) {
    size_t len = strlen(s);
    ssize_t n = write(fd, s, len);
    return (n == (ssize_t)len) ? 0 : -1;
}

static const char* level_name(log_level_t l) {
    switch (l) {
        case LOG_LEVEL_DEBUG: return "DEBUG";
        case LOG_LEVEL_INFO:  return "INFO";
        case LOG_LEVEL_ERROR: return "ERROR";
        default:              return "UNKNOWN";
    }
}

static int expect_record(const char* needle, log_stream_t expected_stream, log_level_t expected_level) {
    const record_t* r = find_record(needle);
    if (r == NULL) {
        diag("FAIL: expected line \"%s\" but it was not delivered", needle);
        return 1;
    }
    if (r->stream != expected_stream) {
        diag("FAIL: line \"%s\" reported stream=%d, expected %d",
             needle, (int)r->stream, (int)expected_stream);
        return 1;
    }
    if (r->level != expected_level) {
        diag("FAIL: line \"%s\" reported level=%s, expected %s",
             needle, level_name(r->level), level_name(expected_level));
        return 1;
    }
    return 0;
}

int main(void) {
    if (logging_init("test_vmlogger_level") != 0) {
        diag("FAIL: logging_init");
        return 1;
    }
    if (logging_add_custom_output(record_callback, &g_callback_context) != 0) {
        diag("FAIL: logging_add_custom_output");
        logging_shutdown();
        return 1;
    }

    int vmlogger_fd     = logging_get_stream_fd(LOG_STREAM_VMLOGGER);
    int ruby_stdout_fd  = logging_get_stream_fd(LOG_STREAM_RUBY_STDOUT);
    int ruby_stderr_fd  = logging_get_stream_fd(LOG_STREAM_RUBY_STDERR);
    if (vmlogger_fd < 0 || ruby_stdout_fd < 0 || ruby_stderr_fd < 0) {
        diag("FAIL: logging_get_stream_fd vmlogger=%d stdout=%d stderr=%d",
             vmlogger_fd, ruby_stdout_fd, ruby_stderr_fd);
        logging_remove_custom_output(record_callback, &g_callback_context);
        logging_shutdown();
        return 1;
    }

    /* VMLogger lines with tags — these mirror what safe_runner.rb emits. The
     * payload (right of "<<<VMLOG:X>>> ") is what the callback should see;
     * the tag itself must be stripped. */
    int write_rc = 0;
    write_rc |= write_line(vmlogger_fd, "<<<VMLOG:DEBUG>>> vmlogger debug message\n");
    write_rc |= write_line(vmlogger_fd, "<<<VMLOG:INFO>>> vmlogger info message\n");
    write_rc |= write_line(vmlogger_fd, "<<<VMLOG:ERROR>>> vmlogger error message\n");

    /* Untagged VMLogger line: should fall back to LOG_LEVEL_INFO with no
     * payload stripping (preserves data from any legacy emitter). */
    write_rc |= write_line(vmlogger_fd, "untagged vmlogger line\n");

    /* Non-VMLOGGER streams: level is derived from the FD. RUBY_STDOUT -> INFO,
     * RUBY_STDERR -> ERROR. The tag prefix does not apply (parse is gated on
     * stream == VMLOGGER), so even a stray prefix would be passed through. */
    write_rc |= write_line(ruby_stdout_fd, "plain ruby stdout\n");
    write_rc |= write_line(ruby_stderr_fd, "plain ruby stderr\n");

    if (write_rc != 0) {
        diag("FAIL: at least one write() to a logging FD failed");
        logging_remove_custom_output(record_callback, &g_callback_context);
        logging_shutdown();
        return 1;
    }

    /* Synchronously drain — guarantees the worker has dispatched every line
     * we wrote above before we inspect g_records. 2s is generous; in practice
     * the queue is empty within milliseconds. */
    int drain_rc = logging_drain(2000);
    if (drain_rc != 0) {
        diag("FAIL: logging_drain returned %d", drain_rc);
        logging_remove_custom_output(record_callback, &g_callback_context);
        logging_shutdown();
        return 1;
    }

    int rc = 0;
    rc |= expect_record("vmlogger debug message", LOG_STREAM_VMLOGGER, LOG_LEVEL_DEBUG);
    rc |= expect_record("vmlogger info message",  LOG_STREAM_VMLOGGER, LOG_LEVEL_INFO);
    rc |= expect_record("vmlogger error message", LOG_STREAM_VMLOGGER, LOG_LEVEL_ERROR);
    rc |= expect_record("untagged vmlogger line", LOG_STREAM_VMLOGGER, LOG_LEVEL_INFO);
    rc |= expect_record("plain ruby stdout",      LOG_STREAM_RUBY_STDOUT, LOG_LEVEL_INFO);
    rc |= expect_record("plain ruby stderr",      LOG_STREAM_RUBY_STDERR, LOG_LEVEL_ERROR);

    logging_remove_custom_output(record_callback, &g_callback_context);
    logging_shutdown();

    if (rc != 0) {
        diag("FAIL: VMLogger level propagation regressed");
        return 1;
    }
    diag("PASS: VMLogger severity tags strip cleanly and surface as log_level_t");
    return 0;
}
