/*
 * The host's fault handlers survive the VM boot (exec-main-vm.c's
 * save_host_fault_handlers / restore_host_fault_handlers).
 *
 * Ruby's init installs its own SIGSEGV / SIGBUS handlers, which abort on any
 * fault Ruby did not cause. A JVM faults on purpose (implicit null checks,
 * stack guard pages, safepoint polls): with Ruby's handlers, any of those
 * aborted the process. So:
 *   1. a fault handler the host installed before the boot is still its own
 *      after the boot, flags included;
 *   2. a fault signal the host left to its default gets Ruby's handler (and
 *      Ruby's crash reports).
 */

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "embedded-ruby-vm/assets-error.h"
#include "embedded-ruby-vm/assets-install.h"
#include "embedded-ruby-vm/log-listener.h"
#include "embedded-ruby-vm/ruby-api-loader.h"

static void diag(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf) - 1, fmt, args);
    va_end(args);
    if (n <= 0) return;
    if (n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;
    buf[n++] = '\n';
    int fd = logging_get_original_stderr_fd();
    ssize_t ignored = write(fd < 0 ? 2 : fd, buf, (size_t)n);
    (void)ignored;
}

/* Never meant to run: a real fault during the test fails it at once */
static void host_fault_handler(int sig, siginfo_t* info, void* context) {
    (void)sig; (void)info; (void)context;
    _exit(3);
}

static void on_log(LogListener* listener, const char* line, log_stream_t stream, log_level_t level, int id) {
    (void)listener; (void)stream; (void)id;
    if (level == LOG_LEVEL_ERROR) diag("[ruby-err] %s", line);
}

int main(void) {
    int rc = 1;
    RubyAPI api;
    RubyInterpreter* interpreter = NULL;

    struct sigaction host;
    memset(&host, 0, sizeof(host));
    host.sa_sigaction = host_fault_handler;
    host.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigaction(SIGSEGV, &host, NULL);
    signal(SIGBUS, SIG_DFL);

    AssetsError assets_error;
    assets_error_init(&assets_error);
    AssetsLayout* layout = assets_bootstrap("./test-ruby-install-fault-handlers", &assets_error);
    if (layout == NULL) {
        diag("FAIL: bootstrap: %s", assets_error.message);
        return 1;
    }
    const char* deps_paths[] = { "../lib/libembedded-ruby.deps", "./libembedded-ruby.deps", "libembedded-ruby.deps", NULL };
    const char* lib_paths[] = { "../lib/libembedded-ruby.so", "./libembedded-ruby.so", "libembedded-ruby.so", NULL };
    if (ruby_api_bootstrap(&api, deps_paths, lib_paths, layout->native_libs_dir) != 0) {
        diag("FAIL: ruby_api_bootstrap");
        goto cleanup;
    }
    LogListener listener = { .context = NULL, .user_data = NULL, .on_log_message = on_log };
    interpreter = api.interpreter.create(".", layout->ruby_stdlib_path, layout->native_libs_dir, listener);
    RubyScript* script = interpreter ? api.script.create_from_content("1 + 1", 5) : NULL;
    if (script == NULL || api.interpreter.execute_sync(interpreter, script) != 0) {
        diag("FAIL: booting the VM");
        goto cleanup;
    }
    api.script.destroy(script);

    struct sigaction segv, bus;
    sigaction(SIGSEGV, NULL, &segv);
    sigaction(SIGBUS, NULL, &bus);
    if (segv.sa_sigaction != host_fault_handler || !(segv.sa_flags & SA_ONSTACK)) {
        diag("FAIL: the host's SIGSEGV handler was replaced by the VM boot");
        goto cleanup;
    }
    if (bus.sa_handler == SIG_DFL) {
        diag("FAIL: SIGBUS, left to its default by the host, did not get Ruby's handler");
        goto cleanup;
    }

    diag("PASS: test_fault_handlers");
    rc = 0;

cleanup:
    if (interpreter) api.interpreter.destroy(interpreter);
    assets_free_layout(layout);
    ruby_api_unload(&api);
    return rc;
}
