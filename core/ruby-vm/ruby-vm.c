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

/**
 * Helper structure for thread communication
 */
typedef struct {
    RubyVM* vm;
    char* ruby_base_directory;
    char* native_libs_location;
} RubyVMStartArgs;

/**
 * Initialize the script queue
 *
 * @param queue Pointer to the script queue to initialize
 */
static void script_queue_init(ScriptQueue* queue) {
    queue->head = NULL;
    queue->tail = NULL;
    queue->shutdown = 0;
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->cond, NULL);
}

/**
 * Destroy the script queue and free all resources
 *
 * @param queue Pointer to the script queue to destroy
 */
static void script_queue_destroy(ScriptQueue* queue) {
    pthread_mutex_lock(&queue->mutex);

    // Free all remaining nodes
    ScriptQueueNode* current = queue->head;
    while (current != NULL) {
        ScriptQueueNode* next = current->next;
        free(current);
        current = next;
    }

    queue->head = NULL;
    queue->tail = NULL;

    pthread_mutex_unlock(&queue->mutex);
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->cond);
}

/**
 * Push a script to the queue
 *
 * @param queue Pointer to the script queue
 * @param script Script to enqueue
 * @param on_complete Completion callback
 */
static void script_queue_push(ScriptQueue* queue, RubyScript* script, RubyCompletionTask on_complete) {
    ScriptQueueNode* node = malloc(sizeof(ScriptQueueNode));
    if (!node) {
        fprintf(stderr, "Failed to allocate queue node\n");
        ruby_completion_task_invoke(&on_complete, 1);
        return;
    }

    node->script = script;
    node->on_complete = on_complete;
    node->next = NULL;

    pthread_mutex_lock(&queue->mutex);

    if (queue->tail == NULL) {
        // Queue is empty
        queue->head = node;
        queue->tail = node;
    } else {
        // Add to tail
        queue->tail->next = node;
        queue->tail = node;
    }

    // Signal worker thread that new work is available
    pthread_cond_signal(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);
}

/**
 * Pop a script from the queue (blocking)
 *
 * @param queue Pointer to the script queue
 * @param script Output pointer for the script
 * @param on_complete Output pointer for the completion callback
 * @return 1 if a script was popped, 0 if queue is shutting down
 */
static int script_queue_pop(ScriptQueue* queue, RubyScript** script, RubyCompletionTask* on_complete) {
    pthread_mutex_lock(&queue->mutex);

    // Wait for work or shutdown signal
    while (queue->head == NULL && !queue->shutdown) {
        pthread_cond_wait(&queue->cond, &queue->mutex);
    }

    // Check if we're shutting down
    if (queue->shutdown && queue->head == NULL) {
        pthread_mutex_unlock(&queue->mutex);
        return 0;
    }

    // Pop from head
    ScriptQueueNode* node = queue->head;
    queue->head = node->next;
    if (queue->head == NULL) {
        queue->tail = NULL;
    }

    *script = node->script;
    *on_complete = node->on_complete;

    pthread_mutex_unlock(&queue->mutex);

    free(node);
    return 1;
}

/**
 * Signal the queue to shutdown
 *
 * @param queue Pointer to the script queue
 */
static void script_queue_shutdown(ScriptQueue* queue) {
    pthread_mutex_lock(&queue->mutex);
    queue->shutdown = 1;
    pthread_cond_broadcast(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);
}


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
 * Worker thread function that processes scripts from the async queue
 *
 * @param arg Pointer to the Ruby VM instance
 * @return NULL
 */
static void* worker_thread_func(void* arg) {
    RubyVM* vm = (RubyVM*)arg;
    RubyScript* script;
    RubyCompletionTask on_complete;

    DEBUG_LOG("Worker thread started");

    // Process scripts until shutdown
    while (script_queue_pop(&vm->script_queue, &script, &on_complete)) {
        char result = 1; // Default to error
        const char* content = ruby_script_get_content(script);

        DEBUG_LOG("Worker thread processing script");

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

        // Invoke completion callback
        ruby_completion_task_invoke(&on_complete, result);

        DEBUG_LOG("Worker thread finished processing script");
    }

    DEBUG_LOG("Worker thread shutting down");
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
    script_queue_init(&vm->script_queue);
    return vm;
}

void ruby_vm_destroy(RubyVM* vm) {
    if (!vm) return;

    // Shutdown the script queue and wait for worker thread to finish
    if (vm->vm_started) {
        DEBUG_LOG("ruby_vm_destroy: Shutting down script queue");
        script_queue_shutdown(&vm->script_queue);

        DEBUG_LOG("ruby_vm_destroy: Waiting for worker thread to finish");
        pthread_join(vm->worker_thread, NULL);
        DEBUG_LOG("ruby_vm_destroy: Worker thread finished");
    }

    // Destroy the queue
    script_queue_destroy(&vm->script_queue);

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

    // Start worker thread for async script execution
    DEBUG_LOG("ruby_vm_start: Creating worker thread");
    thread_result = pthread_create(&vm->worker_thread, NULL, worker_thread_func, vm);
    if (thread_result != 0) {
        DEBUG_LOG("ruby_vm_start: Failed to create worker thread");
        ruby_vm_error_set(&vm->last_error, RUBY_VM_ERROR_THREAD_CREATE,
                          "Failed to create worker thread (error code: %d)", thread_result);
        // Note: main thread is already running, but we can't handle scripts without worker
        return RUBY_VM_ERROR_THREAD_CREATE;
    }
    DEBUG_LOG("ruby_vm_start: Worker thread created");

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
    // Push script to async queue - worker thread will process it
    script_queue_push(&vm->script_queue, script, on_complete);
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
