#ifndef INTERPRETER_H
#define INTERPRETER_H

#include "embedded-ruby-vm/log-listener.h"
#include "embedded-ruby-vm/completion-task.h"
#include "embedded-ruby-vm/ruby-script-location.h"

#ifdef __cplusplus
extern "C" {
#endif

struct RubyVM;
struct RubyScript;

typedef struct RubyVM RubyVM;
typedef struct RubyScript RubyScript;

struct RubyInterpreter {
    char* application_path;
    char* ruby_base_directory;
    char* native_libs_location;
    RubyVM* vm;
    LogListener log_listener;
};
typedef struct RubyInterpreter RubyInterpreter;

/**
 * Create a Ruby interpreter.
 *
 * @param listener Log listener with callbacks. All fields must be initialized
 *                 (use log_listener_init() to zero-init before setting callbacks).
 */
RubyInterpreter* ruby_interpreter_create(const char* application_path,
                                       const char* ruby_base_directory,
                                       const char* native_libs_location,
                                       LogListener listener);
void ruby_interpreter_destroy(RubyInterpreter* interpreter);
int ruby_interpreter_enqueue(RubyInterpreter* interpreter, RubyScript* script, RubyCompletionTask on_complete );
int ruby_interpreter_execute_sync(RubyInterpreter* interpreter, RubyScript* script);
int ruby_interpreter_enable_logging(RubyInterpreter* interpreter);
int ruby_interpreter_disable_logging(RubyInterpreter* interpreter);
// Error handling - delegates to underlying VM
const char* ruby_interpreter_get_error_message(const RubyInterpreter* interpreter);

#ifdef __cplusplus
}
#endif

#endif
