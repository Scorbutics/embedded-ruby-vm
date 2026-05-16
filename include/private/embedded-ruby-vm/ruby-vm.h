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
#include "embedded-ruby-vm/remote-options.h"

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

/* `RubyVMRemoteDebugOptions` and `RubyVMRemoteEvalOptions` are declared in
 * remote-options.h (included above) so thin consumers — the
 * NativeActivity wrapper, JNI callers — can use them without dragging in
 * pthread / atomic / CommChannel / LogListener. */

/* Forward decl — definition is in ruby-vm.c. Each in-flight script holds a
 * waiter on the per-VM FIFO list; the reader thread pops the front waiter
 * for a matching interpreter_id when a response arrives. */
typedef struct result_waiter result_waiter_t;

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

    /* send_lock protects writes to commands_channel.main_fd. A request
     * (interp_id + length + content) is written atomically under this lock
     * AND the waiter is registered under it, so that the FIFO ordering of
     * registrations matches the FIFO ordering of bytes on the wire. */
    pthread_mutex_t send_lock;

    /* reader_thread drains responses (interp_id + exit_code) from the
     * command channel and signals the matching waiter. Spawned by
     * ruby_vm_start_setup, joined by ruby_vm_destroy after EOF on the wire. */
    pthread_t reader_thread;
    atomic_int reader_thread_started;  // 0/1 — only join if it was spawned

    /* FIFO list of pending waiters, protected by waiters_lock. The list is
     * shared across all interpreter ids; on completion we walk from the
     * head and pop the first waiter whose interpreter_id matches — that
     * preserves request ordering both globally (head of list = oldest) and
     * per-interpreter (the dispatcher and worker preserve per-id FIFO on
     * the Ruby side). */
    pthread_mutex_t waiters_lock;
    result_waiter_t* waiters_head;
    result_waiter_t* waiters_tail;

    RubyVMError last_error;
    /* NULL unless ruby_vm_enable_remote_debug() was called. When non-NULL,
     * the VM has duplicated the caller's strings and owns them — freed in
     * ruby_vm_destroy. */
    RubyVMRemoteDebugOptions* remote_debug;
    /* NULL unless ruby_vm_enable_remote_eval() was called. Same ownership
     * contract as remote_debug above. */
    RubyVMRemoteEvalOptions* remote_eval;
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
 * Enqueue a Ruby script to be executed (asynchronously via native pthread).
 *
 * The script is tagged with `interpreter_id` so the Ruby-side dispatcher
 * routes it to that interpreter's worker Thread. Scripts targeting the
 * same id are processed sequentially by that worker; scripts targeting
 * different ids run on separate Ruby Threads and share the GVL
 * cooperatively. See execute_script_internal in ruby-vm.c.
 *
 * @param vm Pointer to the Ruby VM instance
 * @param interpreter_id Owning interpreter's id (RubyInterpreter::interpreter_id)
 * @param script Ruby script to enqueue
 * @param on_complete Completion callback
 */
void ruby_vm_enqueue(RubyVM* vm, int interpreter_id, RubyScript* script, RubyCompletionTask on_complete);

/**
 * Execute a Ruby script synchronously on the calling thread.
 * This function BLOCKS until the script completes.
 *
 * IMPORTANT: Call this from a JVM thread, NOT from main thread or other critical threads.
 * This avoids native pthreads attaching to JVM.
 *
 * @param vm Pointer to the Ruby VM instance
 * @param interpreter_id Owning interpreter's id (RubyInterpreter::interpreter_id)
 * @param script Ruby script to execute
 * @return 0 on success, non-zero on error
 */
int ruby_vm_execute_sync(RubyVM* vm, int interpreter_id, RubyScript* script);

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

/* RubyVMRemoteDebugOptions is defined in
 * `embedded-ruby-vm/remote-options.h` (auto-included via ruby-vm.h's
 * include list). See that file for field semantics and ownership rules. */

/**
 * Enable the remote DAP debugger on the given VM. Must be called BEFORE
 * ruby_vm_start() or ruby_vm_start_on_current_thread() (i.e. while the VM
 * state is CREATED). The Ruby `debug` gem's TCP listener is armed in
 * non-stop mode at VM boot, so a DAP client can attach at any time during
 * the VM's lifetime, including before the first script is enqueued.
 *
 * The token argument is REQUIRED — even when binding to loopback — to make
 * accidental misconfiguration safer. It is propagated to the debug gem via
 * RUBY_DEBUG_COOKIE; clients must echo it in their handshake.
 *
 * @param vm   Pointer to the Ruby VM instance (must be in CREATED state)
 * @param opts Listener options; copied internally — caller retains ownership
 *             of the passed strings
 * @return RUBY_VM_OK on success, RUBY_VM_ERROR_INVALID_PARAM for bad input,
 *         RUBY_VM_ERROR_ALREADY_STARTED if called after start
 */
int ruby_vm_enable_remote_debug(RubyVM* vm, const RubyVMRemoteDebugOptions* opts);

/* RubyVMRemoteEvalOptions is defined in
 * `embedded-ruby-vm/remote-options.h` (auto-included). */

/**
 * Enable the remote line-eval console on the given VM. Must be called BEFORE
 * ruby_vm_start() or ruby_vm_start_on_current_thread() (state must be
 * CREATED). At VM boot, remote_eval.rb is loaded and `RemoteEval.start` is
 * called, which spawns a Ruby thread accepting on the configured TCP socket.
 *
 * Token is REQUIRED. Validated server-side via a one-line handshake before
 * any eval runs; mismatches drop the connection without echoing what the
 * client sent.
 *
 * @return RUBY_VM_OK, RUBY_VM_ERROR_INVALID_PARAM, or
 *         RUBY_VM_ERROR_ALREADY_STARTED.
 */
int ruby_vm_enable_remote_eval(RubyVM* vm, const RubyVMRemoteEvalOptions* opts);

#ifdef __cplusplus
}
#endif

#endif // RUBY_VM_H
