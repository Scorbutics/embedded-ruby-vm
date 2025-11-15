#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <use_direct_memory.h>

#include "constants.h"
#include "ruby-script-location.h"
#include "ruby-vm.h"
#include "ruby-script.h"
#include "ruby-interpreter.h"

static RubyVM* g_global_vm = NULL; // Static VM instance

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

        // Build main script
        RubyScript* main_script = ruby_script_create_from_content(
                get_in_memory_file_content(FIFO_INTERPRETER_SCRIPT),
                get_in_memory_file_size(FIFO_INTERPRETER_SCRIPT)
        );
        if (!main_script) {
            ruby_completion_task_invoke(&on_complete, 1);
            return 1;
        }

        g_global_vm = ruby_vm_create(interpreter->application_path, main_script, interpreter->log_listener);
        if (!g_global_vm) {
            ruby_script_destroy(main_script);
            ruby_completion_task_invoke(&on_complete, 2);
            return 2;
        }

        if (ruby_vm_start(g_global_vm, interpreter->ruby_base_directory, interpreter->native_libs_location) != 0) {
            return 3;
        }
    } else {
        // Update log listener
        g_global_vm->log_listener = interpreter->log_listener;
    }

    ruby_vm_enqueue(g_global_vm, script, on_complete);
    return 0;
}
