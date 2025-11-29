#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <use_direct_memory.h>

#include "constants.h"
#include "ruby-script-location.h"
#include "ruby-vm.h"
#include "ruby-script.h"
#include "ruby-interpreter.h"
#include "debug.h"

// Static global VM instance
static RubyVM* g_global_vm = NULL;

RubyInterpreter* ruby_interpreter_create(const char* application_path,
                                       const char* ruby_base_directory,
                                       const char* native_libs_location,
                                       LogListener listener) {
    RubyInterpreter* interpreter = malloc(sizeof(RubyInterpreter));
    if (!interpreter) return NULL;

    interpreter->application_path = strdup(application_path);
    interpreter->ruby_base_directory = strdup(ruby_base_directory);
    interpreter->native_libs_location = strdup(native_libs_location);
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

int ruby_interpreter_enqueue(RubyInterpreter* interpreter, RubyScript* script, RubyCompletionTask on_complete) {

    if (g_global_vm == NULL) {
        DEBUG_LOG("Creating VM for first time");

        // Build main script
        DEBUG_LOG("Creating FIFO interpreter script");
        RubyScript* main_script = ruby_script_create_from_content(
                get_in_memory_file_content(FIFO_INTERPRETER_SCRIPT),
                get_in_memory_file_size(FIFO_INTERPRETER_SCRIPT)
        );
        if (!main_script) {
            DEBUG_LOG("Failed to create main script");
            ruby_completion_task_invoke(&on_complete, 1);
            return 1;
        }

        DEBUG_LOG("Calling ruby_vm_create()");
        g_global_vm = ruby_vm_create(interpreter->application_path, main_script, interpreter->log_listener);
        if (!g_global_vm) {
            DEBUG_LOG("ruby_vm_create() failed");
            ruby_script_destroy(main_script);
            ruby_completion_task_invoke(&on_complete, 2);
            return 2;
        }

        // Store VM reference in interpreter for error access
        interpreter->vm = g_global_vm;

        DEBUG_LOG("Calling ruby_vm_start()");
        int start_result = ruby_vm_start(g_global_vm, interpreter->ruby_base_directory, interpreter->native_libs_location);
        if (start_result != 0) {
            DEBUG_LOG("ruby_vm_start() failed with code: %d", start_result);
            DEBUG_LOG("Error message: %s", ruby_vm_get_error_message(g_global_vm));
            ruby_completion_task_invoke(&on_complete, 3);
            return start_result;
        }
        DEBUG_LOG("VM started successfully");
    } else {
        // Update log listener and VM reference
        g_global_vm->log_listener = interpreter->log_listener;
        interpreter->vm = g_global_vm;
    }

    DEBUG_LOG("Enqueueing script");
    ruby_vm_enqueue(g_global_vm, script, on_complete);
    DEBUG_LOG("Script enqueued");
    return 0;
}

int ruby_interpreter_enable_logging(RubyInterpreter* interpreter) {
    if (!interpreter || !interpreter->vm) {
        return -1;
    }
    return ruby_vm_enable_logging(interpreter->vm);
}

int ruby_interpreter_disable_logging(RubyInterpreter* interpreter) {
    if (!interpreter || !interpreter->vm) {
        return -1;
    }
    return ruby_vm_disable_logging(interpreter->vm);
}

const char* ruby_interpreter_get_error_message(const RubyInterpreter* interpreter) {
    if (!interpreter || !interpreter->vm) {
        return "Interpreter not initialized";
    }
    return ruby_vm_get_error_message(interpreter->vm);
}
