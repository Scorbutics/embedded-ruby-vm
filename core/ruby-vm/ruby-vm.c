#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <pthread.h>

#include "constants.h"

#include "logging.h"
#include "ruby-script-location.h"
#include "ruby-script.h"
#include "ruby-vm.h"
#include "exec-main-vm.h"
#include "debug.h"

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
    LoggingThreadStop();

    // Close communication channels
    close_comm_channel(&vm->commands_channel);

    free(vm->application_path);
    free(vm);
}

static void native_log_callbacks(const char* line, log_stream_t stream, void* context) {
    RubyVM* vm = (RubyVM*)context;
    if (stream == LOG_STREAM_STDOUT && vm->log_listener.accept) {
        vm->log_listener.accept(&vm->log_listener, line);
    } else if (stream == LOG_STREAM_STDERR && vm->log_listener.on_log_error) {
        vm->log_listener.on_log_error(&vm->log_listener, line);
    }
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
    int thread_result = pthread_create(&vm->main_thread, NULL, ruby_vm_main_thread_func, transferredMemoryArgs);
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

/**
 * Enable logging with stdout/stderr redirection (OPTIONAL)
 *
 * Call this if you want Ruby's stdout/stderr to be captured through the logging system. 
 * If not called, Ruby output goes to normal stdout/stderr.
 *
 * @return 0 on success, negative on error
 */
int ruby_vm_enable_logging(RubyVM* vm) {

    // Setup log reading callbacks (but don't start logging thread yet)
    DEBUG_LOG("ruby_vm_enable_logging: Setting up logging callbacks");
    LoggingSetCustomOutputCallback(native_log_callbacks, vm);

    DEBUG_LOG("ruby_vm_enable_logging: Starting logging thread");
    int logging_result = LoggingThreadRun("com.scorbutics.rubyvm");

    if (logging_result != 0) {
        DEBUG_LOG("ruby_vm_enable_logging: Logging thread failed to start (error %d)", logging_result);
        DEBUG_LOG("Continuing without logging redirection - output will go to normal stdout/stderr");
        return logging_result;
    }

    DEBUG_LOG("ruby_vm_enable_logging: Logging thread started successfully");
    return 0;
}

void ruby_vm_enqueue(RubyVM* vm, RubyScript* script, RubyCompletionTask on_complete) {
    RubyScriptEnqueueArgs* transferredMemoryArgs = malloc(sizeof(RubyScriptEnqueueArgs));
    transferredMemoryArgs->vm = vm;
    transferredMemoryArgs->script = script;
    transferredMemoryArgs->on_complete = on_complete;

    pthread_t script_thread;
    // "transferredMemoryArgs" is consumed and freed by the thread
    // TODO: implement an Async queue instead
    pthread_create(&script_thread, NULL, ruby_vm_script_thread_func, transferredMemoryArgs);
    pthread_detach(script_thread);
}

// Thread functions
void* ruby_vm_main_thread_func(void* arg) {
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

// Cleanup function - ALWAYS called on thread exit
static void cleanup_script_args(void* arg) {
    RubyScriptEnqueueArgs* args = (RubyScriptEnqueueArgs*)arg;
    free(args);
}

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

void* ruby_vm_script_thread_func(void* arg) {
    RubyScriptEnqueueArgs* args = (RubyScriptEnqueueArgs*)arg;

    pthread_cleanup_push(cleanup_script_args, args);

    RubyVM* vm = args->vm;
    RubyCompletionTask on_complete = args->on_complete;
    char result = 1; // Default to error
    const char* content = ruby_script_get_content(args->script);

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

    // Now command is executed and return code queried, let the place to the next script
    pthread_mutex_unlock(&vm->socket_lock);

    ruby_completion_task_invoke(&on_complete, result);

    // Pop and execute cleanup handler
    // NOTE: argument '1' means EXECUTE the cleanup
    pthread_cleanup_pop(1);

    return NULL;
}

// ============================================================================
// Error Handling API
// ============================================================================

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
