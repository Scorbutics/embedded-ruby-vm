#ifndef INTERPRETER_H
#define INTERPRETER_H

#include "log-listener.h"
#include "completion-task.h"
#include "ruby-script-location.h"

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

RubyInterpreter* ruby_interpreter_create(const char* application_path,
                                       const char* ruby_base_directory,
                                       const char* native_libs_location,
                                       LogListener listener);
void ruby_interpreter_destroy(RubyInterpreter* interpreter);
int ruby_interpreter_enqueue(RubyInterpreter* interpreter, RubyScript* script, RubyCompletionTask on_complete );

// Error handling - delegates to underlying VM
const char* ruby_interpreter_get_error_message(const RubyInterpreter* interpreter);

#ifdef __cplusplus
}
#endif

#endif
