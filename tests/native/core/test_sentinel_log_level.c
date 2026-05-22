/*
 * Regression test: script-completion sentinel must reach the C side even when
 * the VMLogger log level is raised to ERROR.
 *
 * Bug: fifo_interpreter.rb sent the "<<<LOGS_FLUSHED>>>" sentinel via
 * VMLogger.info, which safe_runner.rb filters out when @log_level > LOG_INFO.
 * Release builds set NDEBUG=1, which raises @log_level to LOG_ERROR, so the
 * sentinel was silently dropped and ruby_vm_execute_sync would always burn
 * its full 5s logging_wait_for_sentinel timeout per script.
 *
 * This test forces @log_level = LOG_ERROR via RUBY_VM_LOG_LEVEL=2 (which
 * takes precedence over NDEBUG in safe_runner.rb, so the test reproduces
 * the bug in both Debug and Release builds), then runs a trivial script
 * via execute_sync and asserts it returns well below the 5s sentinel
 * timeout. With the bug, the call takes >= 5s; with the fix, it takes
 * milliseconds.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "embedded-ruby-vm/ruby-api-loader.h"
#include "embedded-ruby-vm/assets-install.h"
#include "embedded-ruby-vm/assets-error.h"
#include "embedded-ruby-vm/log-listener.h"  /* logging_get_original_stderr_fd */

/* Print directly to the pre-redirect terminal stderr so diagnostic output
 * survives the logging-thread-shutdown race at process exit. fprintf(stderr)
 * goes through the logging pipe (STDERR_FILENO is dup2'd) and may be lost if
 * the logging thread tears down before the buffered line is dispatched. */
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

/* Generous threshold: a successful execute_sync should be well under 1s for
 * a no-op script. The sentinel timeout is 5s, so anything >= 2s indicates
 * the sentinel was lost and the wait timed out. */
#define MAX_EXPECTED_DURATION_MS 2000

static RubyAPI ruby_api;

static void on_log(LogListener* listener, const char* line, log_stream_t stream, log_level_t level, int interpreter_id) {
    (void)listener;
    (void)stream;
    (void)interpreter_id;
    if (level == LOG_LEVEL_ERROR) {
        fprintf(stderr, "[ruby-err] %s\n", line);
    } else {
        fprintf(stdout, "[ruby] %s\n", line);
    }
}

static long elapsed_ms(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) * 1000L
         + (end.tv_nsec - start.tv_nsec) / 1000000L;
}

int main(void) {
    int rc = 1;
    AssetsLayout* layout = NULL;
    RubyInterpreter* interpreter = NULL;
    RubyScript* script = NULL;
    AssetsError assets_error;

    /* Force the bug condition regardless of build type. RUBY_VM_LOG_LEVEL is
     * checked before NDEBUG in safe_runner.rb, so this works in Debug too.
     * 2 == LOG_ERROR. Must be set before the Ruby VM starts. */
    if (setenv("RUBY_VM_LOG_LEVEL", "2", 1) != 0) {
        perror("setenv RUBY_VM_LOG_LEVEL");
        return 1;
    }

    assets_error_init(&assets_error);
    layout = assets_bootstrap("./test-ruby-install-sentinel", &assets_error);
    if (layout == NULL) {
        fprintf(stderr, "Bootstrap failed: %s\n", assets_error.message);
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

    if (ruby_api_bootstrap(&ruby_api, deps_paths, lib_paths, layout->native_libs_dir) != 0) {
        fprintf(stderr, "Failed to bootstrap Ruby API\n");
        goto cleanup;
    }

    LogListener listener = {
        .context = NULL,
        .user_data = NULL,
        .on_log_message = on_log
    };

    interpreter = ruby_api.interpreter.create(
        ".",
        layout->ruby_stdlib_path,
        layout->native_libs_dir,
        listener
    );
    if (interpreter == NULL) {
        fprintf(stderr, "Failed to create interpreter\n");
        goto cleanup;
    }

    /* Trivial no-op script. The success/failure of the assertion is purely
     * about how long execute_sync takes to return — which depends on whether
     * the sentinel reaches the C side. */
    const char* script_content = "1 + 1\n";
    script = ruby_api.script.create_from_content(script_content, strlen(script_content));
    if (script == NULL) {
        fprintf(stderr, "Failed to create script\n");
        goto cleanup;
    }

    /* Warm-up: first execute_sync also pays VM init cost. We measure the
     * second call so the timing reflects only the per-script overhead. */
    if (ruby_api.interpreter.execute_sync(interpreter, script) != 0) {
        fprintf(stderr, "Warm-up execute_sync failed\n");
        goto cleanup;
    }

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    int script_rc = ruby_api.interpreter.execute_sync(interpreter, script);
    clock_gettime(CLOCK_MONOTONIC, &end);

    if (script_rc != 0) {
        fprintf(stderr, "execute_sync returned non-zero exit code: %d\n", script_rc);
        goto cleanup;
    }

    long elapsed = elapsed_ms(start, end);
    diag("execute_sync (with RUBY_VM_LOG_LEVEL=2) took %ld ms", elapsed);

    if (elapsed >= MAX_EXPECTED_DURATION_MS) {
        diag("FAIL: execute_sync took %ld ms (>= %d ms threshold).", elapsed, MAX_EXPECTED_DURATION_MS);
        diag("      This indicates logging_wait_for_sentinel timed out, meaning the");
        diag("      script-completion sentinel did not reach the C side. Likely the");
        diag("      VMLogger.info filter is dropping the sentinel under elevated log");
        diag("      level - fifo_interpreter.rb should use VMLogger.protocol (or");
        diag("      another bypass) for the sentinel write.");
        goto cleanup;
    }

    diag("PASS: sentinel reached C side under elevated log level");
    rc = 0;

cleanup:
    if (script != NULL) ruby_api.script.destroy(script);
    if (interpreter != NULL) ruby_api.interpreter.destroy(interpreter);
    if (layout != NULL) assets_free_layout(layout);
    ruby_api_unload(&ruby_api);
    return rc;
}
