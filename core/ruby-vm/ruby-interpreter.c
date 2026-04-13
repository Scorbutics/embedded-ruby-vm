#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "embedded-ruby-vm/constants.h"
#include "embedded-ruby-vm/embedded_scripts.h"
#include "embedded-ruby-vm/ruby-script-location.h"
#include "embedded-ruby-vm/ruby-vm.h"
#include "embedded-ruby-vm/ruby-script.h"
#include "embedded-ruby-vm/ruby-interpreter.h"
#include "embedded-ruby-vm/debug.h"

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

    free(interpreter->application_path);
    free(interpreter->ruby_base_directory);
    free(interpreter->native_libs_location);
    free(interpreter);
}

/**
 * Ensure the global VM is initialized and ready.
 * This is shared initialization logic for both sync and async execution.
 *
 * @param interpreter Interpreter instance with configuration
 * @return 0 on success, error code on failure
 */
static int ensure_vm_initialized(RubyInterpreter* interpreter) {
    pthread_mutex_lock(&g_vm_mutex);

    if (g_global_vm == NULL) {
        DEBUG_LOG("Creating VM for first time");

        // Build main script
        DEBUG_LOG("Creating FIFO interpreter script");
        RubyScript* main_script = ruby_script_create_from_content(
                embedded_script_get_content(FIFO_INTERPRETER_SCRIPT),
                embedded_script_get_size(FIFO_INTERPRETER_SCRIPT)
        );
        if (!main_script) {
            DEBUG_LOG("Failed to create main script");
            pthread_mutex_unlock(&g_vm_mutex);
            return 1;
        }

        DEBUG_LOG("Calling ruby_vm_create()");
        g_global_vm = ruby_vm_create(interpreter->application_path, main_script, interpreter->log_listener);
        if (!g_global_vm) {
            DEBUG_LOG("ruby_vm_create() failed");
            ruby_script_destroy(main_script);
            pthread_mutex_unlock(&g_vm_mutex);
            return 2;
        }

        // Store VM reference in interpreter for error access
        interpreter->vm = g_global_vm;

        DEBUG_LOG("Calling ruby_vm_start()");
        int start_result = ruby_vm_start(g_global_vm, interpreter->ruby_base_directory, interpreter->native_libs_location);
        if (start_result != 0) {
            DEBUG_LOG("ruby_vm_start() failed with code: %d", start_result);
            DEBUG_LOG("Error message: %s", ruby_vm_get_error_message(g_global_vm));
            pthread_mutex_unlock(&g_vm_mutex);
            return start_result;
        }
        DEBUG_LOG("VM started successfully");
    } else {
        // Update log listener under the lock to prevent concurrent reads
        // from the logging thread seeing a partially-written struct
        g_global_vm->log_listener = interpreter->log_listener;
        interpreter->vm = g_global_vm;

        // Re-enable logging if the new interpreter has listener callbacks.
        // A previous interpreter's destroy() may have disabled logging,
        // which stops the logging thread entirely.
        if (interpreter->log_listener.accept != NULL ||
            interpreter->log_listener.on_log_error != NULL ||
            interpreter->log_listener.on_log_message != NULL) {
            ruby_vm_enable_logging(g_global_vm);
        }
    }

    pthread_mutex_unlock(&g_vm_mutex);
    return 0;
}

int ruby_interpreter_enqueue(RubyInterpreter* interpreter, RubyScript* script, RubyCompletionTask on_complete) {
    int init_result = ensure_vm_initialized(interpreter);
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
    int init_result = ensure_vm_initialized(interpreter);
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
    int init_result = ensure_vm_initialized(interpreter);
    if (init_result != 0) {
        return -2;
    }

    return ruby_vm_enable_logging(interpreter->vm);
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
