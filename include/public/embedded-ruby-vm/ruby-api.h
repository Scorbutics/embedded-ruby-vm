#ifndef RUBY_API_H
#define RUBY_API_H

/**
 * Interpreter and script functions of the library.
 *
 * Only public headers are included here: a static build's ruby-api-loader.h
 * declares the functions it points to with this header, and the released
 * include directory holds no private header.
 */

#include <stddef.h>

#include "embedded-ruby-vm/completion-task.h"
#include "embedded-ruby-vm/log-listener.h"
#include "embedded-ruby-vm/remote-options.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque for library users */
typedef struct RubyInterpreter RubyInterpreter;
typedef struct RubyScript RubyScript;

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

/**
 * Enable the remote DAP listener for live debugging.
 *
 * May be called at any point during the interpreter's lifetime — both
 * before and after the VM has started. Both code paths leave you with a
 * listening DAP socket; they only differ in how the listener gets armed:
 *
 *   - VM not started yet: eagerly boots the VM with the listener already
 *     armed (env vars + `require 'debug/open'` during boot).
 *   - VM already started: injects the listener via a synchronous script
 *     enqueued through the FIFO interpreter (`require 'debug/session'` +
 *     `DEBUGGER__.open`). Slightly later, same end state.
 *
 * Calling both this and enable_remote_eval on the same interpreter is
 * supported; whichever is called second uses the inject path.
 *
 * See RubyVMRemoteDebugOptions in remote-options.h for required option semantics.
 *
 * @return RUBY_VM_OK on success, RUBY_VM_ERROR_INVALID_PARAM for bad opts,
 *         RUBY_VM_ERROR_RUBY_EXEC if the inject script fails on the Ruby
 *         side.
 */
int ruby_interpreter_enable_remote_debug(RubyInterpreter* interpreter,
                                         const RubyVMRemoteDebugOptions* opts);

/**
 * Enable the remote line-eval console listener. Same lifecycle semantics
 * as enable_remote_debug: may be called either before or after the VM
 * starts, and can coexist with the debug listener. See
 * RubyVMRemoteEvalOptions in remote-options.h for option semantics.
 */
int ruby_interpreter_enable_remote_eval(RubyInterpreter* interpreter,
                                        const RubyVMRemoteEvalOptions* opts);

// Error handling - delegates to underlying VM
const char* ruby_interpreter_get_error_message(const RubyInterpreter* interpreter);

RubyScript* ruby_script_create_from_content(const char* content, size_t content_size);
void ruby_script_destroy(RubyScript* script);

#ifdef __cplusplus
}
#endif

#endif /* RUBY_API_H */
