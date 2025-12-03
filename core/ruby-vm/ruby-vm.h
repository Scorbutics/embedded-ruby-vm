#ifndef RUBY_VM_H
#define RUBY_VM_H

#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "ruby-comm-channel.h"
#include "log-listener.h"
#include "completion-task.h"
#include "ruby-vm-error.h"

struct RubyScript;
struct RubyScriptCurrentLocation;

typedef struct RubyScript RubyScript;
typedef struct RubyScriptCurrentLocation RubyScriptCurrentLocation;

struct RubyVM {
    char* application_path;
    RubyScript* main_script;
    pthread_t main_thread;
    CommChannel commands_channel;
    LogListener log_listener;
    int vm_started;
    pthread_mutex_t socket_lock;
    RubyVMError last_error;
};
typedef struct RubyVM RubyVM;

/**
 * Create a new Ruby VM instance
 *
 * @param application_path Path to the application directory
 * @param main_script Main Ruby script to execute
 * @param listener Log listener for receiving log messages
 * @return Pointer to the created Ruby VM instance, or NULL on failure
 */
RubyVM* ruby_vm_create(const char* application_path, RubyScript* main_script, LogListener listener);

/**
 * Destroy a Ruby VM instance
 *
 * @param vm Pointer to the Ruby VM instance to destroy
 */
void ruby_vm_destroy(RubyVM* vm);

/**
 * Start the Ruby VM
 *
 * @param vm Pointer to the Ruby VM instance to start
 * @param ruby_base_directory Path to the Ruby base directory
 * @param native_libs_location Path to the native libraries location
 * @return 0 on success, negative on error
 */
int ruby_vm_start(RubyVM* vm, const char* ruby_base_directory, const char* native_libs_location);

/**
 * Enable logging with stdout/stderr redirection
 *
 * Call this if you want Ruby's stdout/stderr to be captured through the logging system. 
 * If not called, Ruby output goes to normal stdout/stderr.
 *
 * @return 0 on success, negative on error
 */
int ruby_vm_enable_logging(RubyVM* vm);

/**
 * Disable logging with stdout/stderr redirection
 *
 * @return 0 on success, negative on error
 */
int ruby_vm_disable_logging(RubyVM* vm);

/**
 * Enqueue a Ruby script to be executed
 *
 * @param vm Pointer to the Ruby VM instance
 * @param script Ruby script to enqueue
 * @param on_complete Completion callback
 */
void ruby_vm_enqueue(RubyVM* vm, RubyScript* script, RubyCompletionTask on_complete);

/**
 * Get the last error that occurred in the Ruby VM
 *
 * @param vm Pointer to the Ruby VM instance
 * @return Pointer to the last error, or NULL if no error occurred
 */
const RubyVMError* ruby_vm_get_last_error(const RubyVM* vm);

/**
 * Clear the last error in the Ruby VM
 *
 * @param vm Pointer to the Ruby VM instance
 */
void ruby_vm_clear_error(RubyVM* vm);

/**
 * Get the error message for the last error that occurred in the Ruby VM
 *
 * @param vm Pointer to the Ruby VM instance
 * @return Error message, or NULL if no error occurred
 */
const char* ruby_vm_get_error_message(const RubyVM* vm);

#ifdef __cplusplus
}
#endif

#endif // RUBY_VM_H
