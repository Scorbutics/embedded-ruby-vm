#ifndef RUBY_VM_H
#define RUBY_VM_H

#include <pthread.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "embedded-ruby-vm/ruby-comm-channel.h"
#include "embedded-ruby-vm/log-listener.h"
#include "embedded-ruby-vm/completion-task.h"
#include "embedded-ruby-vm/ruby-vm-error.h"

struct RubyScript;
struct RubyScriptCurrentLocation;

typedef struct RubyScript RubyScript;
typedef struct RubyScriptCurrentLocation RubyScriptCurrentLocation;

/**
 * VM lifecycle states (monotonically increasing — never goes backward)
 */
typedef enum {
    RUBY_VM_STATE_CREATED = 0,       // Created but not yet started
    RUBY_VM_STATE_RUNNING = 1,       // Started and accepting scripts
    RUBY_VM_STATE_SHUTTING_DOWN = 2, // Destroy requested, draining in-flight scripts
    RUBY_VM_STATE_DESTROYED = 3      // Fully destroyed, no further access allowed
} RubyVMState;

struct RubyVM {
    char* application_path;
    RubyScript* main_script;
    pthread_t main_thread;
    CommChannel commands_channel;
    LogListener log_listener;
    atomic_int state;              // RubyVMState — lifecycle state machine
    atomic_int in_flight_scripts;  // ref-count of active script execution threads
    pthread_mutex_t drain_mutex;   // protects drain_cond
    pthread_cond_t drain_cond;     // signaled when in_flight_scripts reaches 0
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
 * Start the Ruby VM on the current thread (blocks until VM shuts down).
 *
 * Unlike ruby_vm_start() which spawns a dedicated pthread, this runs the
 * FIFO interpreter inline on the calling thread. Use this when the VM must
 * run on a specific thread (e.g., an EGL/ALooper thread).
 *
 * Scripts can be enqueued from other threads via ruby_vm_enqueue() while
 * this function is blocking.
 *
 * @param vm Pointer to the Ruby VM instance
 * @param ruby_base_directory Path to the Ruby base directory
 * @param native_libs_location Path to the native libraries location
 * @return Ruby exit code (0 on success)
 */
int ruby_vm_start_on_current_thread(RubyVM* vm, const char* ruby_base_directory,
                                     const char* native_libs_location);

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
 * Enqueue a Ruby script to be executed (asynchronously via native pthread)
 *
 * @param vm Pointer to the Ruby VM instance
 * @param script Ruby script to enqueue
 * @param on_complete Completion callback
 */
void ruby_vm_enqueue(RubyVM* vm, RubyScript* script, RubyCompletionTask on_complete);

/**
 * Execute a Ruby script synchronously on the calling thread.
 * This function BLOCKS until the script completes.
 *
 * IMPORTANT: Call this from a JVM thread, NOT from main thread or other critical threads.
 * This avoids native pthreads attaching to JVM.
 *
 * @param vm Pointer to the Ruby VM instance
 * @param script Ruby script to execute
 * @return 0 on success, non-zero on error
 */
int ruby_vm_execute_sync(RubyVM* vm, RubyScript* script);

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
