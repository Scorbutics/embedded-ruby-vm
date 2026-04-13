#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <poll.h>
#include <stdatomic.h>
#include "embedded-ruby-vm/constants.h"

#include "embedded-ruby-vm/logging.h"
#include "embedded-ruby-vm/ruby-script-location.h"
#include "embedded-ruby-vm/ruby-script.h"
#include "embedded-ruby-vm/ruby-vm.h"
#include "embedded-ruby-vm/exec-main-vm.h"
#include "embedded-ruby-vm/debug.h"

/**
 * Helper structure for thread communication
 */
typedef struct {
    RubyVM* vm;
    char* ruby_base_directory;
    char* native_libs_location;
} RubyVMStartArgs;

/**
 * Helper structure for script execution thread
 */
typedef struct {
    RubyVM* vm;
    RubyScript* script;
    RubyCompletionTask on_complete;
} ScriptExecutionArgs;


/**
 * Main thread function for the Ruby VM
 *
 * @param arg Pointer to the Ruby VM instance
 * @return NULL
 */
static void* main_thread_func(void* arg) {
    RubyVMStartArgs* args = (RubyVMStartArgs*)arg;
    RubyVM* vm = args->vm;

    const int exitCode = ExecMainRubyVM(vm,
        args->ruby_base_directory,
        args->native_libs_location
    );

    if (exitCode != 0) {
        fprintf(stderr, "Error during VM execution: %d", exitCode);
    }

    free(args->native_libs_location);
    free(args->ruby_base_directory);
    free(args);
    return NULL;
}

/**
 * Write all bytes to a file descriptor, retrying on partial writes and EINTR.
 *
 * @param fd File descriptor
 * @param buf Data to write
 * @param count Number of bytes to write
 * @return 0 on success, -1 on error
 */
static int write_all(int fd, const void* buf, size_t count) {
    size_t total = 0;
    while (total < count) {
        ssize_t n = write(fd, (const char*)buf + total, count - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        total += (size_t)n;
    }
    return 0;
}

/**
 * Send a script to the Ruby VM
 *
 * @param socket_fd Socket file descriptor
 * @param script_content Script content to send
 * @return 0 on success, negative on error
 */
static int send_script_to_ruby(int socket_fd, const char* script_content) {
    size_t script_length = strlen(script_content);
    char length_buffer[32];

    // Send length prefix: "<length>\n"
    int written = snprintf(length_buffer, sizeof(length_buffer), "%zu\n", script_length);
    if (write_all(socket_fd, length_buffer, (size_t)written) != 0) {
        perror("Failed to write length prefix");
        return -1;
    }

    // Send script content (no trailing newline needed)
    if (write_all(socket_fd, script_content, script_length) != 0) {
        perror("Failed to write script content");
        return -1;
    }
    return 0;
}

// Default timeout for waiting on script exit code (in milliseconds).
// Set to 0 for no timeout (block indefinitely).
#define SCRIPT_READ_TIMEOUT_MS 0

/**
 * Read the exit code response from the Ruby VM.
 * The protocol is: "<exit_code>\n" where exit_code is a decimal integer (0-255).
 *
 * @param fd Socket file descriptor
 * @param timeout_ms Timeout in milliseconds (0 = no timeout)
 * @param exit_code Output parameter for the parsed exit code
 * @return 0 on success, -1 on error/timeout
 */
static int read_exit_code(int fd, int timeout_ms, int* exit_code) {
    // Wait for data with optional timeout
    if (timeout_ms > 0) {
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        int poll_result;
        do {
            poll_result = poll(&pfd, 1, timeout_ms);
        } while (poll_result < 0 && errno == EINTR);

        if (poll_result == 0) {
            fprintf(stderr, "Timeout waiting for script exit code (%d ms)\n", timeout_ms);
            return -1;
        }
        if (poll_result < 0) {
            perror("poll() failed waiting for exit code");
            return -1;
        }
    }

    // Read response byte-by-byte until newline (max 4 bytes: up to "255\n")
    char buf[8];
    int pos = 0;
    while (pos < (int)(sizeof(buf) - 1)) {
        ssize_t n = read(fd, &buf[pos], 1);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("Failed to read exit code");
            return -1;
        }
        if (n == 0) {
            fprintf(stderr, "protocol error: EOF while reading exit code\n");
            return -1;
        }
        if (buf[pos] == '\n') {
            buf[pos] = '\0';
            *exit_code = atoi(buf);
            return 0;
        }
        pos++;
    }

    fprintf(stderr, "protocol error: exit code too long (no newline in %d bytes)\n", pos);
    return -1;
}

/**
 * Execute a script and return the exit code.
 * This is the core execution logic shared between sync and async execution.
 * Caller is responsible for signal masking if needed.
 *
 * @param vm Ruby VM instance
 * @param script Script to execute
 * @return Exit code (0 = success, non-zero = error)
 */
static int execute_script_internal(RubyVM* vm, RubyScript* script) {
    int result = 1; // Default to error
    const char* content = ruby_script_get_content(script);

    // Lock for entire transaction
    pthread_mutex_lock(&vm->socket_lock);

    // Write commands as VM socket input
    if (send_script_to_ruby(vm->commands_channel.main_fd, content) != 0) {
        fprintf(stderr, "Failed to send script to Ruby VM\n");
        pthread_mutex_unlock(&vm->socket_lock);
        return 1;
    }

    // Read exit code response
    if (read_exit_code(vm->commands_channel.main_fd, SCRIPT_READ_TIMEOUT_MS, &result) != 0) {
        fprintf(stderr, "Failed to read exit code from Ruby VM\n");
        result = 1;
    }

    // Unlock for next script
    pthread_mutex_unlock(&vm->socket_lock);

    return result;
}

/**
 * Script execution thread function - executes a single script and terminates
 *
 * @param arg Pointer to ScriptExecutionArgs
 * @return NULL
 */
static void* script_execution_thread_func(void* arg) {
    ScriptExecutionArgs* exec_args = (ScriptExecutionArgs*)arg;
    RubyVM* vm = exec_args->vm;
    RubyScript* script = exec_args->script;
    RubyCompletionTask on_complete = exec_args->on_complete;

    // Free args immediately since we've copied what we need
    free(exec_args);

    // Block all signals to prevent Ruby's signal handlers from affecting this thread
    // Ruby's GC uses signals to stop threads, but this is a native thread, not a Ruby thread
    sigset_t set;
    sigfillset(&set);
    pthread_sigmask(SIG_BLOCK, &set, NULL);

    DEBUG_LOG("Script execution thread started");

    // Execute the script
    const int result = execute_script_internal(vm, script);

    DEBUG_LOG("Script execution thread finished - invoking completion callback");

    // Invoke completion callback
    ruby_completion_task_invoke(&on_complete, result);

    // Decrement in-flight count and signal drain if this was the last script
    if (atomic_fetch_sub(&vm->in_flight_scripts, 1) == 1) {
        // We were the last in-flight script — wake up ruby_vm_destroy() if it's waiting
        pthread_mutex_lock(&vm->drain_mutex);
        pthread_cond_signal(&vm->drain_cond);
        pthread_mutex_unlock(&vm->drain_mutex);
    }

    DEBUG_LOG("Script execution thread finished - terminating");
    return NULL;
}

static int native_log_callbacks(const char* line, log_stream_t stream, void* context) {
    if (context == NULL) {
        return -1;
    }

    RubyVM* vm = (RubyVM*)context;

    // Prefer new callback with source information if available
    if (vm->log_listener.on_log_message != NULL) {
        vm->log_listener.on_log_message(&vm->log_listener, line, stream);
        return 0;
    }

    // Fall back to legacy callbacks for backward compatibility
    // Map new stream types to old callbacks as best as possible
    switch (stream) {
        case LOG_STREAM_RUBY_STDOUT:
        case LOG_STREAM_NATIVE_STDOUT:
        case LOG_STREAM_VMLOGGER:  // VMLogger info/debug goes to stdout callback
            if (vm->log_listener.accept == NULL) {
                // No accept handler defined
                return -2;
            }
            vm->log_listener.accept(&vm->log_listener, line);
            break;

        case LOG_STREAM_RUBY_STDERR:
        case LOG_STREAM_NATIVE_STDERR:
            if (vm->log_listener.on_log_error == NULL) {
                // No error handler defined
                return -2;
            }
            vm->log_listener.on_log_error(&vm->log_listener, line);
            break;

        default:
            // Unknown stream type
            return -3;
    }

    return 0;
}

RubyVM* ruby_vm_create(const char* application_path, RubyScript* main_script, LogListener listener) {
    if (!application_path || !main_script) return NULL;

    RubyVM* vm = malloc(sizeof(RubyVM));
    if (!vm) return NULL;

    vm->application_path = strdup(application_path);
    if (!vm->application_path) {
        free(vm);
        return NULL;
    }
    vm->main_script = main_script;
    vm->log_listener = listener;
    atomic_store(&vm->state, RUBY_VM_STATE_CREATED);
    atomic_store(&vm->in_flight_scripts, 0);
    pthread_mutex_init(&vm->drain_mutex, NULL);
    pthread_cond_init(&vm->drain_cond, NULL);
    pthread_mutex_init(&vm->socket_lock, NULL);
    ruby_vm_error_init(&vm->last_error);
    return vm;
}

void ruby_vm_destroy(RubyVM* vm) {
    if (!vm) return;

    // Transition to SHUTTING_DOWN — reject new enqueues from this point
    int was_running = 0;
    int expected = RUBY_VM_STATE_RUNNING;
    if (atomic_compare_exchange_strong(&vm->state, &expected, RUBY_VM_STATE_SHUTTING_DOWN)) {
        was_running = 1;
    } else {
        // If not RUNNING, it may be CREATED (never started) — try that too
        expected = RUBY_VM_STATE_CREATED;
        if (!atomic_compare_exchange_strong(&vm->state, &expected, RUBY_VM_STATE_SHUTTING_DOWN)) {
            DEBUG_LOG("ruby_vm_destroy: VM already shutting down or destroyed (state=%d)", expected);
            return;
        }
    }

    DEBUG_LOG("ruby_vm_destroy: Draining in-flight scripts");
    // Wait for all in-flight script threads to complete
    pthread_mutex_lock(&vm->drain_mutex);
    while (atomic_load(&vm->in_flight_scripts) > 0) {
        DEBUG_LOG("ruby_vm_destroy: Waiting for %d in-flight script(s)", atomic_load(&vm->in_flight_scripts));
        pthread_cond_wait(&vm->drain_cond, &vm->drain_mutex);
    }
    pthread_mutex_unlock(&vm->drain_mutex);
    DEBUG_LOG("ruby_vm_destroy: All scripts drained");

    if (was_running) {
        DEBUG_LOG("ruby_vm_destroy: Closing command channel to signal FIFO interpreter exit");
        // Close the command channel FIRST — this causes the Ruby FIFO interpreter
        // to see EOF on socket.gets and break out of its loop cleanly.
        // Logging must stay alive during this phase so the Ruby VM thread can
        // still write to stdout/stderr/vmlogger without hitting broken pipes.
        close_comm_channel(&vm->commands_channel);

        DEBUG_LOG("ruby_vm_destroy: Joining main VM thread");
        // Wait for the Ruby VM thread to fully exit (including Ruby cleanup).
        // Without this join, ruby_run_node() may still be running when we tear
        // down logging pipes and free the VM struct, causing SIGPIPE storms
        // and potentially exit_group() killing the entire process.
        pthread_join(vm->main_thread, NULL);
        DEBUG_LOG("ruby_vm_destroy: Main VM thread joined");
    }

    DEBUG_LOG("ruby_vm_destroy: Disabling logging for VM");
    ruby_vm_disable_logging(vm);

    // Destroy synchronization primitives
    pthread_mutex_destroy(&vm->socket_lock);
    pthread_cond_destroy(&vm->drain_cond);
    pthread_mutex_destroy(&vm->drain_mutex);

    // Mark as fully destroyed before freeing
    atomic_store(&vm->state, RUBY_VM_STATE_DESTROYED);

    DEBUG_LOG("ruby_vm_destroy: Freeing VM memory");
    free(vm->application_path);
    free(vm);
}

/**
 * Shared setup for ruby_vm_start() and ruby_vm_start_on_current_thread().
 * Validates state, enables logging, and creates the communication channel.
 *
 * @param vm Pointer to the Ruby VM instance
 * @return RUBY_VM_OK on success, negative error code on failure
 */
static int ruby_vm_start_setup(RubyVM* vm) {
    if (!vm) {
        return RUBY_VM_ERROR_INVALID_PARAM;
    }

    // Only start from CREATED state
    int expected = RUBY_VM_STATE_CREATED;
    if (!atomic_compare_exchange_strong(&vm->state, &expected, RUBY_VM_STATE_CREATED)) {
        // State was not CREATED — already started or shutting down
        ruby_vm_error_set(&vm->last_error, RUBY_VM_ERROR_ALREADY_STARTED,
                          "VM is already started (state=%d)", expected);
        return RUBY_VM_ERROR_ALREADY_STARTED;
    }

    // Clear any previous errors
    ruby_vm_clear_error(vm);

    // Auto-enable logging if a listener was provided
    // This ensures log streams are available before Ruby VM needs them
    if (vm->log_listener.accept != NULL || vm->log_listener.on_log_error != NULL || vm->log_listener.on_log_message != NULL) {
        DEBUG_LOG("ruby_vm_start_setup: Auto-enabling logging (listener provided)");
        int logging_result = ruby_vm_enable_logging(vm);
        if (logging_result != 0) {
            DEBUG_LOG("ruby_vm_start_setup: Warning - failed to enable logging (error %d)", logging_result);
            // Continue anyway - VM can still work without logging
        }
    }

    DEBUG_LOG("ruby_vm_start_setup: Creating socket pair");
    // Create socket pair for communication
    if (create_comm_channel(&vm->commands_channel) != 0) {
        DEBUG_LOG("ruby_vm_start_setup: Failed to create comm channel");
        ruby_vm_error_set(&vm->last_error, RUBY_VM_ERROR_COMM_CHANNEL,
                          "Failed to create communication channel (socketpair failed)");
        return RUBY_VM_ERROR_COMM_CHANNEL;
    }
    DEBUG_LOG("ruby_vm_start_setup: Socket pair created");

    return RUBY_VM_OK;
}

int ruby_vm_start(RubyVM* vm, const char* ruby_base_directory, const char* native_libs_location) {
    int setup_result = ruby_vm_start_setup(vm);
    if (setup_result != RUBY_VM_OK) return setup_result;

    // Create thread arguments
    DEBUG_LOG("ruby_vm_start: Preparing thread args");
    RubyVMStartArgs* transferredMemoryArgs = malloc(sizeof(RubyVMStartArgs));
    if (!transferredMemoryArgs) {
        ruby_vm_error_set(&vm->last_error, RUBY_VM_ERROR_INVALID_PARAM,
                          "Failed to allocate memory for VM start args");
        return RUBY_VM_ERROR_INVALID_PARAM;
    }
    transferredMemoryArgs->vm = vm;
    transferredMemoryArgs->ruby_base_directory = strdup(ruby_base_directory);
    transferredMemoryArgs->native_libs_location = strdup(native_libs_location);

    if (!transferredMemoryArgs->ruby_base_directory || !transferredMemoryArgs->native_libs_location) {
        ruby_vm_error_set(&vm->last_error, RUBY_VM_ERROR_INVALID_PARAM,
                          "Failed to allocate memory for VM path strings");
        free(transferredMemoryArgs->ruby_base_directory);
        free(transferredMemoryArgs->native_libs_location);
        free(transferredMemoryArgs);
        return RUBY_VM_ERROR_INVALID_PARAM;
    }

    // Start main thread
    // "transferredMemoryArgs" is consumed and freed by the main thread
    DEBUG_LOG("ruby_vm_start: Creating main VM thread");
    int thread_result = pthread_create(&vm->main_thread, NULL, main_thread_func, transferredMemoryArgs);
    if (thread_result != 0) {
        DEBUG_LOG("ruby_vm_start: Failed to create main VM thread");
        ruby_vm_error_set(&vm->last_error, RUBY_VM_ERROR_THREAD_CREATE,
                          "Failed to create Ruby VM thread (error code: %d)", thread_result);
        free(transferredMemoryArgs->ruby_base_directory);
        free(transferredMemoryArgs->native_libs_location);
        free(transferredMemoryArgs);
        return RUBY_VM_ERROR_THREAD_CREATE;
    }
    DEBUG_LOG("ruby_vm_start: Main VM thread created");

    atomic_store(&vm->state, RUBY_VM_STATE_RUNNING);
    DEBUG_LOG("ruby_vm_start: VM started successfully, returning");
    return RUBY_VM_OK;
}

int ruby_vm_start_on_current_thread(RubyVM* vm, const char* ruby_base_directory,
                                     const char* native_libs_location) {
    int setup_result = ruby_vm_start_setup(vm);
    if (setup_result != RUBY_VM_OK) return setup_result;

    atomic_store(&vm->state, RUBY_VM_STATE_RUNNING);
    DEBUG_LOG("ruby_vm_start_on_current_thread: VM running, executing FIFO interpreter inline");

    // Run the FIFO interpreter inline — this BLOCKS until the socket is closed
    const int exitCode = ExecMainRubyVM(vm, ruby_base_directory, native_libs_location);

    if (exitCode != 0) {
        fprintf(stderr, "Error during inline VM execution: %d", exitCode);
    }

    DEBUG_LOG("ruby_vm_start_on_current_thread: FIFO interpreter exited with code %d", exitCode);
    return exitCode;
}

int ruby_vm_enable_logging(RubyVM* vm) {

    // Setup log reading callbacks (but don't start logging thread yet)
    DEBUG_LOG("ruby_vm_enable_logging: Setting up logging callbacks");
    
    // TODO add a way to override the tag
    const int init_result = logging_init("com.scorbutics.rubyvm");
    const int logging_result = init_result != 0 ? init_result : logging_add_custom_output(native_log_callbacks, vm);

    if (logging_result != 0) {
        DEBUG_LOG("ruby_vm_enable_logging: Logging thread failed to start (error %d during %s)", logging_result, init_result != 0 ? "initialization": "adding custom output");
        DEBUG_LOG("Continuing without logging redirection - output will go to normal stdout/stderr");
        ruby_vm_error_set(&vm->last_error, RUBY_VM_ERROR_LOGGING,
                          "Failed to start logging thread (error code: %d)", logging_result);
        return logging_result;
    }

    DEBUG_LOG("ruby_vm_enable_logging: Logging thread started successfully");
    return 0;
}

int ruby_vm_disable_logging(RubyVM* vm) {
    DEBUG_LOG("ruby_vm_disable_logging: Removing logging callback for this VM");

    // CRITICAL FIX: We should remove only THIS VM's callback, not shutdown everything!
    // logging_shutdown() would shut down the entire logging system for ALL VMs.
    // Instead, we remove just this VM's custom output callback.
    // The logging thread will automatically stop when the last callback is removed.
    const int result = logging_remove_custom_output(native_log_callbacks, vm);

    if (result != 0) {
        DEBUG_LOG("ruby_vm_disable_logging: Failed to remove logging callback (error %d)", result);
        // Note: -1 means callback was not found, which could mean it was never added
        // or already removed. This is not necessarily a fatal error.
        if (result == -1) {
            DEBUG_LOG("ruby_vm_disable_logging: Callback not found (may not have been enabled)");
            return 0; // Not an error - logging may not have been enabled
        }
        ruby_vm_error_set(&vm->last_error, RUBY_VM_ERROR_LOGGING,
                          "Failed to remove logging callback (error code: %d)", result);
        return result;
    }

    DEBUG_LOG("ruby_vm_disable_logging: Logging callback removed successfully");
    return 0;
}

void ruby_vm_enqueue(RubyVM* vm, RubyScript* script, RubyCompletionTask on_complete) {
    if (!vm || !script) {
        DEBUG_LOG("ruby_vm_enqueue: Invalid parameters");
        ruby_completion_task_invoke(&on_complete, 1);
        return;
    }

    // Check VM state and atomically increment in-flight count
    // We must increment BEFORE checking state to avoid a race with destroy
    atomic_fetch_add(&vm->in_flight_scripts, 1);

    if (atomic_load(&vm->state) != RUBY_VM_STATE_RUNNING) {
        DEBUG_LOG("ruby_vm_enqueue: VM is not running (state=%d), rejecting script", atomic_load(&vm->state));
        atomic_fetch_sub(&vm->in_flight_scripts, 1);
        ruby_completion_task_invoke(&on_complete, 1);
        return;
    }

    // Allocate args for the execution thread
    ScriptExecutionArgs* args = malloc(sizeof(ScriptExecutionArgs));
    if (!args) {
        fprintf(stderr, "Failed to allocate script execution args\n");
        atomic_fetch_sub(&vm->in_flight_scripts, 1);
        ruby_completion_task_invoke(&on_complete, 1);
        return;
    }

    args->vm = vm;
    args->script = script;
    args->on_complete = on_complete;

    // Create a new thread to execute this script
    pthread_t execution_thread;
    int thread_result = pthread_create(&execution_thread, NULL, script_execution_thread_func, args);
    if (thread_result != 0) {
        fprintf(stderr, "Failed to create script execution thread (error code: %d)\n", thread_result);
        free(args);
        atomic_fetch_sub(&vm->in_flight_scripts, 1);
        ruby_completion_task_invoke(&on_complete, 1);
        return;
    }

    // Detach the thread so it cleans up automatically when done
    pthread_detach(execution_thread);

    DEBUG_LOG("ruby_vm_enqueue: Script execution thread created and detached");
}

int ruby_vm_execute_sync(RubyVM* vm, RubyScript* script) {
    if (!vm || !script) {
        DEBUG_LOG("ruby_vm_execute_sync: Invalid parameters");
        return 1;
    }

    // Check VM state and increment in-flight count (same pattern as enqueue)
    atomic_fetch_add(&vm->in_flight_scripts, 1);

    if (atomic_load(&vm->state) != RUBY_VM_STATE_RUNNING) {
        DEBUG_LOG("ruby_vm_execute_sync: VM is not running (state=%d)", atomic_load(&vm->state));
        atomic_fetch_sub(&vm->in_flight_scripts, 1);
        return 1;
    }

    DEBUG_LOG("ruby_vm_execute_sync: Executing script synchronously on calling thread");

    // Reset the sentinel before execution so we can wait for a fresh one
    logging_reset_sentinel();

    // Block all signals before entering Ruby execution context
    // This prevents Ruby's GC signals (SIGPROF/SIGALRM) from hitting JVM threads
    // which can cause segmentation faults when Ruby's signal handler tries to
    // interact with JVM-managed memory.
    //
    // Context: Ruby's GC uses signals to coordinate thread pausing during the
    // marking phase. When a JVM thread calls this function and Ruby's GC runs,
    // the signal can be delivered to the JVM thread. Ruby's signal handler may
    // attempt to scan the thread's stack for Ruby objects, but the JVM thread
    // has a JVM-managed stack, not a Ruby thread stack. This mismatch causes
    // a segmentation fault, typically manifesting AFTER the native call returns
    // and the JVM thread continues execution.
    sigset_t new_set, old_set;
    sigfillset(&new_set);
    pthread_sigmask(SIG_BLOCK, &new_set, &old_set);

    // Execute the script using shared internal implementation
    const int result = execute_script_internal(vm, script);

    // Restore original signal mask before returning to JVM context
    pthread_sigmask(SIG_SETMASK, &old_set, NULL);

    // Wait for all logs to be flushed before returning.
    // The logging thread intercepts the sentinel on LOG_STREAM_VMLOGGER
    // (processed after RUBY_STDOUT and RUBY_STDERR), ensuring all
    // user-visible output has been dispatched to callbacks.
    if (logging_wait_for_sentinel(5000) != 0) {
        DEBUG_LOG("ruby_vm_execute_sync: Timeout waiting for log flush sentinel");
    }

    // Decrement in-flight count and signal drain if this was the last script
    if (atomic_fetch_sub(&vm->in_flight_scripts, 1) == 1) {
        pthread_mutex_lock(&vm->drain_mutex);
        pthread_cond_signal(&vm->drain_cond);
        pthread_mutex_unlock(&vm->drain_mutex);
    }

    DEBUG_LOG("ruby_vm_execute_sync: Script execution completed with result: %d", result);

    return result;
}

const RubyVMError* ruby_vm_get_last_error(const RubyVM* vm) {
    if (!vm) return NULL;
    return &vm->last_error;
}

void ruby_vm_clear_error(RubyVM* vm) {
    if (!vm) return;
    ruby_vm_error_init(&vm->last_error);
}

const char* ruby_vm_get_error_message(const RubyVM* vm) {
    if (!vm) return "Invalid VM pointer";
    if (vm->last_error.code == RUBY_VM_OK) return NULL;

    if (vm->last_error.message[0] != '\0') {
        return vm->last_error.message;
    }
    return ruby_vm_error_string(vm->last_error.code);
}
