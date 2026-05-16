#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdarg.h>
#include <unistd.h>

#include "embedded-ruby-vm/constants.h"
#include "embedded-ruby-vm/embedded_scripts.h"
#include "embedded-ruby-vm/ruby-script-location.h"
#include "embedded-ruby-vm/ruby-vm.h"
#include "embedded-ruby-vm/ruby-script.h"
#include "embedded-ruby-vm/ruby-interpreter.h"
#include "embedded-ruby-vm/log-listener.h"  /* logging_get_original_stderr_fd */
#include "embedded-ruby-vm/logging.h"       /* logging_swap_listener */
#include "embedded-ruby-vm/debug.h"

/* Diagnostic log helper that bypasses the logging pipe — see ruby_vm_jni.c. */
static void diag_log(const char* fmt, ...) {
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

// Static global VM instance, protected by g_vm_mutex
static RubyVM* g_global_vm = NULL;
static pthread_mutex_t g_vm_mutex = PTHREAD_MUTEX_INITIALIZER;

RubyInterpreter* ruby_interpreter_create(const char* application_path,
                                       const char* ruby_base_directory,
                                       const char* native_libs_location,
                                       LogListener listener) {
    RubyInterpreter* interpreter = malloc(sizeof(RubyInterpreter));
    if (!interpreter) return NULL;

    interpreter->application_path = strdup(application_path);
    interpreter->ruby_base_directory = strdup(ruby_base_directory);
    interpreter->native_libs_location = strdup(native_libs_location);

    if (!interpreter->application_path || !interpreter->ruby_base_directory || !interpreter->native_libs_location) {
        free(interpreter->application_path);
        free(interpreter->ruby_base_directory);
        free(interpreter->native_libs_location);
        free(interpreter);
        return NULL;
    }

    interpreter->log_listener = listener;
    interpreter->vm = NULL;

    return interpreter;
}

void ruby_interpreter_destroy(RubyInterpreter* interpreter) {
    if (!interpreter) return;

    /* If this interpreter's listener is still bound to the global VM, swap
     * it out under the logging system's lock before the caller frees any
     * context referenced by the listener. Without this, the logging thread
     * keeps draining buffered Ruby output and dispatching through the
     * dangling listener — a UAF that crashes once the freed chunk is reused. */
    pthread_mutex_lock(&g_vm_mutex);
    if (g_global_vm != NULL &&
        interpreter->vm == g_global_vm &&
        g_global_vm->log_listener.context == interpreter->log_listener.context) {
        LogListener empty;
        log_listener_init(&empty);
        logging_swap_listener(&g_global_vm->log_listener, empty);
    }
    pthread_mutex_unlock(&g_vm_mutex);

    free(interpreter->application_path);
    free(interpreter->ruby_base_directory);
    free(interpreter->native_libs_location);
    free(interpreter);
}

/* Handle the "VM already running" branch — swap in the new interpreter's
 * log listener (if different), point interpreter->vm at the global, and
 * re-enable logging if needed. Caller must hold g_vm_mutex. */
static void rebind_existing_vm_locked(RubyInterpreter* interpreter) {
    /* Only swap if the listener actually differs — this is called on every
     * execute_sync, so an unconditional struct copy would be pure overhead. */
    if (memcmp(&g_global_vm->log_listener, &interpreter->log_listener,
               sizeof(LogListener)) != 0) {
        diag_log("[JNI-DIAG] ensure_vm_initialized: swapping listener on g_global_vm=%p old_ctx=%p new_ctx=%p",
                 (void*)g_global_vm,
                 (void*)g_global_vm->log_listener.context,
                 (void*)interpreter->log_listener.context);
        /* Take the logging lock so any in-flight callback dispatch finishes
         * before we overwrite the listener — see logging_swap_listener. */
        logging_swap_listener(&g_global_vm->log_listener, interpreter->log_listener);
    }
    interpreter->vm = g_global_vm;

    /* Re-enable logging if the new interpreter has a listener callback.
     * A previous interpreter's destroy() may have disabled logging, which
     * stops the logging thread entirely. */
    if (interpreter->log_listener.on_log_message != NULL) {
        ruby_vm_enable_logging(g_global_vm);
    }
}

/**
 * Ensure the global VM is initialized and ready.
 * This is shared initialization logic for both sync and async execution.
 *
 * @param interpreter   Interpreter instance with configuration
 * @param remote_debug  Optional remote-debug options applied after ruby_vm_create
 *                      and before ruby_vm_start. NULL means "no remote debug".
 *                      Ignored if the VM is already created.
 * @param remote_eval   Optional remote-eval options (sibling of remote_debug);
 *                      same lifecycle constraint.
 * @return 0 on success, error code on failure
 *
 * Error handling is a single-exit goto-to-cleanup pattern: each step's
 * failure path branches to the right label, which destroys *only* the
 * resources successfully acquired so far. The function has one unlock
 * site, one VM-teardown site, and one return — so adding a future
 * initialization step is one new `goto` line, and the cleanup is already
 * correct.
 */
static int ensure_vm_initialized(RubyInterpreter* interpreter,
                                 const RubyVMRemoteDebugOptions* remote_debug,
                                 const RubyVMRemoteEvalOptions* remote_eval) {
    int rc = 0;
    RubyScript* main_script = NULL;
    /* Ownership flag for main_script: true while we still need to free it
     * ourselves. Becomes false the moment ruby_vm_create succeeds, since
     * the VM then owns the script. */
    int main_script_owned = 0;
    /* True once g_global_vm has been set by us during THIS call — gates
     * the destroy-on-failure path. (We never destroy a VM we didn't just
     * create, even if g_global_vm is non-NULL.) */
    int vm_created_here = 0;

    pthread_mutex_lock(&g_vm_mutex);

    if (g_global_vm != NULL) {
        /* Fast path: VM already running, just rebind to this interpreter. */
        rebind_existing_vm_locked(interpreter);
        goto out_unlock;
    }

    DEBUG_LOG("Creating VM for first time");

    DEBUG_LOG("Creating FIFO interpreter script");
    main_script = ruby_script_create_from_content(
            embedded_script_get_content(FIFO_INTERPRETER_SCRIPT),
            embedded_script_get_size(FIFO_INTERPRETER_SCRIPT));
    if (!main_script) {
        DEBUG_LOG("Failed to create main script");
        rc = 1;
        goto out_unlock;
    }
    main_script_owned = 1;

    DEBUG_LOG("Calling ruby_vm_create()");
    g_global_vm = ruby_vm_create(interpreter->application_path, main_script, interpreter->log_listener);
    if (!g_global_vm) {
        DEBUG_LOG("ruby_vm_create() failed");
        rc = 2;
        goto out_destroy_script;
    }
    /* Ownership transferred to the VM. */
    main_script_owned = 0;
    vm_created_here = 1;
    interpreter->vm = g_global_vm;

    if (remote_debug != NULL) {
        DEBUG_LOG("Applying remote debug options before start");
        rc = ruby_vm_enable_remote_debug(g_global_vm, remote_debug);
        if (rc != RUBY_VM_OK) {
            DEBUG_LOG("ruby_vm_enable_remote_debug() failed: %d", rc);
            goto out_destroy_vm;
        }
    }

    if (remote_eval != NULL) {
        DEBUG_LOG("Applying remote eval options before start");
        rc = ruby_vm_enable_remote_eval(g_global_vm, remote_eval);
        if (rc != RUBY_VM_OK) {
            DEBUG_LOG("ruby_vm_enable_remote_eval() failed: %d", rc);
            goto out_destroy_vm;
        }
    }

    DEBUG_LOG("Calling ruby_vm_start()");
    rc = ruby_vm_start(g_global_vm, interpreter->ruby_base_directory, interpreter->native_libs_location);
    if (rc != 0) {
        DEBUG_LOG("ruby_vm_start() failed with code: %d", rc);
        DEBUG_LOG("Error message: %s", ruby_vm_get_error_message(g_global_vm));
        /* Fix vs. the pre-refactor code: tear down the created-but-never-
         * started VM, so the caller can retry rather than being stuck with
         * g_global_vm permanently pointing at a CREATED-state VM that
         * future ensure_vm_initialized() calls would treat as "already up". */
        goto out_destroy_vm;
    }
    DEBUG_LOG("VM started successfully");
    goto out_unlock;

    /* ---- error cleanup, single exit through out_unlock ---- */
out_destroy_vm:
    if (vm_created_here) {
        ruby_vm_destroy(g_global_vm);
        g_global_vm = NULL;
        interpreter->vm = NULL;
    }
out_destroy_script:
    if (main_script_owned) {
        ruby_script_destroy(main_script);
    }
out_unlock:
    pthread_mutex_unlock(&g_vm_mutex);
    return rc;
}

int ruby_interpreter_enqueue(RubyInterpreter* interpreter, RubyScript* script, RubyCompletionTask on_complete) {
    int init_result = ensure_vm_initialized(interpreter, NULL, NULL);
    if (init_result != 0) {
        ruby_completion_task_invoke(&on_complete, init_result);
        return init_result;
    }

    DEBUG_LOG("Enqueueing script");
    ruby_vm_enqueue(g_global_vm, script, on_complete);
    DEBUG_LOG("Script enqueued");
    return 0;
}

int ruby_interpreter_execute_sync(RubyInterpreter* interpreter, RubyScript* script) {
    int init_result = ensure_vm_initialized(interpreter, NULL, NULL);
    if (init_result != 0) {
        return init_result;
    }

    DEBUG_LOG("Executing script synchronously");
    int result = ruby_vm_execute_sync(g_global_vm, script);
    DEBUG_LOG("Script execution completed with result: %d", result);
    return result;
}

int ruby_interpreter_enable_logging(RubyInterpreter* interpreter) {
    if (!interpreter) {
        return -1;
    }

    // Ensure VM is initialized before enabling logging
    // This allows enableLogging() to be called before the first script execution
    int init_result = ensure_vm_initialized(interpreter, NULL, NULL);
    if (init_result != 0) {
        return -2;
    }

    return ruby_vm_enable_logging(interpreter->vm);
}

/* Validate opts at the interpreter layer with the SAME rules
 * ruby_vm_enable_remote_debug applies — this lets us reject bad opts before
 * we ever decide between boot-path vs inject-path. */
static int validate_remote_debug_opts(const RubyVMRemoteDebugOptions* opts) {
    if (!opts)                                       return RUBY_VM_ERROR_INVALID_PARAM;
    if (opts->port <= 0 || opts->port > 65535)       return RUBY_VM_ERROR_INVALID_PARAM;
    if (!opts->token || opts->token[0] == '\0')      return RUBY_VM_ERROR_INVALID_PARAM;
    return RUBY_VM_OK;
}

static int validate_remote_eval_opts(const RubyVMRemoteEvalOptions* opts) {
    if (!opts)                                       return RUBY_VM_ERROR_INVALID_PARAM;
    if (opts->port <= 0 || opts->port > 65535)       return RUBY_VM_ERROR_INVALID_PARAM;
    if (!opts->token || opts->token[0] == '\0')      return RUBY_VM_ERROR_INVALID_PARAM;
    return RUBY_VM_OK;
}

/* Post-boot injection: enqueue a synchronous Ruby script through the FIFO
 * interpreter that arms the listener. Env vars are set from C (no Ruby-side
 * string quoting needed), and the script reads them. Returns RUBY_VM_OK iff
 * the script reported exit code 0.
 *
 * Safe to call from any caller of ruby_interpreter_enable_remote_*; the
 * FIFO interpreter serializes the work onto the Ruby VM main thread. */
static int inject_remote_debug_listener(const RubyVMRemoteDebugOptions* opts) {
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", opts->port);
    setenv("RUBY_DEBUG_HOST",    opts->host ? opts->host : "127.0.0.1", 1);
    setenv("RUBY_DEBUG_PORT",    port_str,                              1);
    setenv("RUBY_DEBUG_COOKIE",  opts->token,                           1);
    setenv("RUBY_DEBUG_NONSTOP", "1",                                   1);
    if (opts->session_name) {
        setenv("RUBY_DEBUG_SESSION_NAME", opts->session_name, 1);
    }

    /* require 'debug/open' would no-op on a second call (Ruby caches the
     * require), so call DEBUGGER__.open directly. require 'debug/session'
     * makes sure the module is loaded before we touch it. */
    const char* src =
        "require 'debug/session' unless defined?(DEBUGGER__)\n"
        "DEBUGGER__.open\n";
    RubyScript* script = ruby_script_create_from_content(src, strlen(src));
    if (!script) return RUBY_VM_ERROR_OUT_OF_MEMORY;
    int rc = ruby_vm_execute_sync(g_global_vm, script);
    ruby_script_destroy(script);
    return rc == 0 ? RUBY_VM_OK : RUBY_VM_ERROR_RUBY_EXEC;
}

static int inject_remote_eval_listener(const RubyVMRemoteEvalOptions* opts) {
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", opts->port);
    setenv("REMOTE_EVAL_HOST",  opts->host ? opts->host : "127.0.0.1", 1);
    setenv("REMOTE_EVAL_PORT",  port_str,                              1);
    setenv("REMOTE_EVAL_TOKEN", opts->token,                           1);
    if (opts->session_name) {
        setenv("REMOTE_EVAL_SESSION_NAME", opts->session_name, 1);
    } else {
        unsetenv("REMOTE_EVAL_SESSION_NAME");
    }

    /* RemoteEval module is pre-loaded at VM boot (always), so we can just
     * call RemoteEval.start here. The method is idempotent — a second call
     * with the listener already up is a no-op. */
    const char* src =
        "RemoteEval.start(host: ENV['REMOTE_EVAL_HOST'],"
        " port: Integer(ENV['REMOTE_EVAL_PORT']),"
        " token: ENV['REMOTE_EVAL_TOKEN'],"
        " session_name: ENV['REMOTE_EVAL_SESSION_NAME'])\n";
    RubyScript* script = ruby_script_create_from_content(src, strlen(src));
    if (!script) return RUBY_VM_ERROR_OUT_OF_MEMORY;
    int rc = ruby_vm_execute_sync(g_global_vm, script);
    ruby_script_destroy(script);
    return rc == 0 ? RUBY_VM_OK : RUBY_VM_ERROR_RUBY_EXEC;
}

int ruby_interpreter_enable_remote_debug(RubyInterpreter* interpreter,
                                         const RubyVMRemoteDebugOptions* opts) {
    if (!interpreter) return RUBY_VM_ERROR_INVALID_PARAM;
    int vrc = validate_remote_debug_opts(opts);
    if (vrc != RUBY_VM_OK) return vrc;

    pthread_mutex_lock(&g_vm_mutex);
    int already_started = (g_global_vm != NULL);
    pthread_mutex_unlock(&g_vm_mutex);

    if (!already_started) {
        /* Boot path: VM not up yet — eagerly create+configure+start with
         * the debug listener armed at boot time. */
        return ensure_vm_initialized(interpreter, opts, NULL);
    }

    /* Post-boot path: VM already up (e.g. enable_remote_eval was called
     * first). Inject the listener via the FIFO interpreter. */
    DEBUG_LOG("ruby_interpreter_enable_remote_debug: VM up — using post-boot inject path");
    return inject_remote_debug_listener(opts);
}

int ruby_interpreter_enable_remote_eval(RubyInterpreter* interpreter,
                                        const RubyVMRemoteEvalOptions* opts) {
    if (!interpreter) return RUBY_VM_ERROR_INVALID_PARAM;
    int vrc = validate_remote_eval_opts(opts);
    if (vrc != RUBY_VM_OK) return vrc;

    pthread_mutex_lock(&g_vm_mutex);
    int already_started = (g_global_vm != NULL);
    pthread_mutex_unlock(&g_vm_mutex);

    if (!already_started) {
        return ensure_vm_initialized(interpreter, NULL, opts);
    }

    DEBUG_LOG("ruby_interpreter_enable_remote_eval: VM up — using post-boot inject path");
    return inject_remote_eval_listener(opts);
}

int ruby_interpreter_disable_logging(RubyInterpreter* interpreter) {
    if (!interpreter) {
        return -1;
    }

    // If VM was never initialized, there's nothing to disable
    if (!interpreter->vm) {
        return 0;
    }

    return ruby_vm_disable_logging(interpreter->vm);
}

const char* ruby_interpreter_get_error_message(const RubyInterpreter* interpreter) {
    if (!interpreter || !interpreter->vm) {
        return "Interpreter not initialized";
    }
    return ruby_vm_get_error_message(interpreter->vm);
}
