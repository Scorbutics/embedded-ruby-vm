#include "embedded-ruby-vm/embedded_scripts.h"
#include <string.h>

// External symbols created by objcopy for embedded scripts
// These symbols are generated when we embed fifo_interpreter.rb

// For fifo_interpreter.rb
extern const char _binary_fifo_interpreter_rb_start[];
extern const char _binary_fifo_interpreter_rb_end[];

// For safe_runner.rb
extern const char _binary_safe_runner_rb_start[];
extern const char _binary_safe_runner_rb_end[];

// For remote_eval.rb
extern const char _binary_remote_eval_rb_start[];
extern const char _binary_remote_eval_rb_end[];

// Structure to map script names to their embedded symbols
typedef struct {
    const char* script_name;
    const char* start;
    const char* end;
} embedded_script_t;

// Registry of all embedded scripts in ruby-vm
static const embedded_script_t embedded_scripts[] = {
    {
        .script_name = "fifo_interpreter.rb",
        .start = _binary_fifo_interpreter_rb_start,
        .end = _binary_fifo_interpreter_rb_end
    },
    {
        .script_name = "safe_runner.rb",
        .start = _binary_safe_runner_rb_start,
        .end = _binary_safe_runner_rb_end
    },
    {
        .script_name = "remote_eval.rb",
        .start = _binary_remote_eval_rb_start,
        .end = _binary_remote_eval_rb_end
    },

    // Sentinel entry
    { .script_name = NULL, .start = NULL, .end = NULL }
};

const char* embedded_script_get_content(const char* script_name) {
    if (script_name == NULL) {
        return NULL;
    }

    for (const embedded_script_t* script = embedded_scripts; script->script_name != NULL; script++) {
        if (strcmp(script->script_name, script_name) == 0) {
            return script->start;
        }
    }

    return NULL;
}

size_t embedded_script_get_size(const char* script_name) {
    if (script_name == NULL) {
        return 0;
    }

    for (const embedded_script_t* script = embedded_scripts; script->script_name != NULL; script++) {
        if (strcmp(script->script_name, script_name) == 0) {
            return (size_t)(script->end - script->start);
        }
    }

    return 0;
}

int embedded_script_exists(const char* script_name) {
    return embedded_script_get_content(script_name) != NULL ? 1 : 0;
}