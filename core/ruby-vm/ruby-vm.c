#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <pthread.h>
#include <signal.h>
#include "constants.h"

#include "logging.h"
#include "ruby-script-location.h"
#include "ruby-script.h"
#include "ruby-vm.h"
#include "exec-main-vm.h"
#include "debug.h"

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

    const int exitCode = ExecMainRubyVM(
        ruby_script_get_content(vm->main_script),
        vm->commands_channel.second_fd,
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
    if (write(socket_fd, length_buffer, written) != written) {
        perror("Failed to write length prefix");
        return -1;
    }
    
    // Send script content (no trailing newline needed)
    if (write(socket_fd, script_content, script_length) != (ssize_t)script_length) {
        perror("Failed to write script content");
        return -1;
    }
    return 0;
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
    char result = 1; // Default to error
    const char* content = ruby_script_get_content(script);

    // Lock for entire transaction
    pthread_mutex_lock(&vm->socket_lock);

    // Write commands as VM socket input
    send_script_to_ruby(vm->commands_channel.main_fd, content);

    // Read exit code + newline as confirmation
    char read_buffer[2] = {0};
    ssize_t bytes_read = read(vm->commands_channel.main_fd, read_buffer, 2);

    if (bytes_read == 2 && read_buffer[1] == '\n') {
        result = read_buffer[0] - '0';
    } else {
        fprintf(stderr, "protocol error: expected 2 bytes, got %zd\n", bytes_read);
    }

    // Unlock for next script
    pthread_mutex_unlock(&vm->socket_lock);

    return (int)result;
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

    DEBUG_LOG("Script execution thread finished - terminating");
    return NULL;
}

static void native_log_callbacks(const char* line, log_stream_t stream, void* context) {
    RubyVM* vm = (RubyVM*)context;
    if (stream == LOG_STREAM_STDOUT && vm->log_listener.accept) {
        vm->log_listener.accept(&vm->log_listener, line);
    } else if (stream == LOG_STREAM_STDERR && vm->log_listener.on_log_error) {
        vm->log_listener.on_log_error(&vm->log_listener, line);
    }
}

RubyVM* ruby_vm_create(const char* application_path, RubyScript* main_script, LogListener listener) {
    if (!application_path || !main_script) return NULL;

    RubyVM* vm = malloc(sizeof(RubyVM));
    if (!vm) return NULL;

    vm->application_path = strdup(application_path);
    vm->main_script = main_script;
    vm->log_listener = listener;
    vm->vm_started = 0;
    pthread_mutex_init(&vm->socket_lock, NULL);
    ruby_vm_error_init(&vm->last_error);
    return vm;
}

void ruby_vm_destroy(RubyVM* vm) {
    if (!vm) return;

    // Stop the logging thread
    ruby_vm_disable_logging(vm);

    // Close communication channels
    close_comm_channel(&vm->commands_channel);

    // Destroy mutex
    pthread_mutex_destroy(&vm->socket_lock);

    free(vm->application_path);
    free(vm);
}

int ruby_vm_start(RubyVM* vm, const char* ruby_base_directory, const char* native_libs_location) {
    if (!vm) {
        return RUBY_VM_ERROR_INVALID_PARAM;
    }

    // Already started
    if (vm->vm_started) {
        ruby_vm_error_set(&vm->last_error, RUBY_VM_ERROR_ALREADY_STARTED,
                          "VM is already started");
        return RUBY_VM_ERROR_ALREADY_STARTED;
    }

    // Clear any previous errors
    ruby_vm_clear_error(vm);

    DEBUG_LOG("ruby_vm_start: Creating socket pair");
    // Create socket pair for communication
    if (create_comm_channel(&vm->commands_channel) != 0) {
        DEBUG_LOG("ruby_vm_start: Failed to create comm channel");
        ruby_vm_error_set(&vm->last_error, RUBY_VM_ERROR_COMM_CHANNEL,
                          "Failed to create communication channel (socketpair failed)");
        return RUBY_VM_ERROR_COMM_CHANNEL;
    }
    DEBUG_LOG("ruby_vm_start: Socket pair created");

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

    vm->vm_started = 1;
    DEBUG_LOG("ruby_vm_start: VM started successfully, returning");
    return RUBY_VM_OK;
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
    DEBUG_LOG("ruby_vm_disable_logging: Stopping logging thread");
    const int result = logging_shutdown();
    if (result != 0) {
        DEBUG_LOG("ruby_vm_disable_logging: Logging thread failed to stop (error %d)", result);
        DEBUG_LOG("Continuing without logging redirection - output will go to normal stdout/stderr");
        ruby_vm_error_set(&vm->last_error, RUBY_VM_ERROR_LOGGING,
                          "Failed to stop logging thread (error code: %d)", result);
        return result;
    }
    DEBUG_LOG("ruby_vm_disable_logging: Logging thread stopped successfully");
    return 0;
}

void ruby_vm_enqueue(RubyVM* vm, RubyScript* script, RubyCompletionTask on_complete) {
    if (!vm || !script) {
        DEBUG_LOG("ruby_vm_enqueue: Invalid parameters");
        ruby_completion_task_invoke(&on_complete, 1);
        return;
    }

    // Allocate args for the execution thread
    ScriptExecutionArgs* args = malloc(sizeof(ScriptExecutionArgs));
    if (!args) {
        fprintf(stderr, "Failed to allocate script execution args\n");
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

    DEBUG_LOG("ruby_vm_execute_sync: Executing script synchronously on calling thread");

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
