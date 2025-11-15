#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <android/log.h>

#include "constants.h"

#include "logging.h"
#include "ruby-script-location.h"
#include "ruby-script.h"
#include "ruby-vm.h"
#include "exec-main-vm.h"

RubyVM* ruby_vm_create(const char* application_path, RubyScript* main_script, LogListener listener) {
    if (!application_path || !main_script) return NULL;

    RubyVM* vm = malloc(sizeof(RubyVM));
    if (!vm) return NULL;

    vm->application_path = strdup(application_path);
    vm->main_script = main_script;
    vm->log_listener = listener;
    vm->vm_started = 0;
    pthread_mutex_init(&vm->socket_lock, NULL);
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
    // Already started, no need to change anything
    if (vm->vm_started) return 1;

    // Create socket pair for communication
    if (create_comm_channel(&vm->commands_channel) != 0) {
        return -1;
    }

    // Create thread arguments
    RubyVMStartArgs* transferredMemoryArgs = malloc(sizeof(RubyVMStartArgs));
    transferredMemoryArgs->vm = vm;
    transferredMemoryArgs->ruby_base_directory = strdup(ruby_base_directory);
    transferredMemoryArgs->native_libs_location = strdup(native_libs_location);

    // Setup log reading callbacks
    LoggingSetCustomOutputCallback(native_log_callbacks, vm);

    // Start logging thread
    LoggingThreadRun("com.scorbutics.rubyvm");

    // Start main thread
    // "transferredMemoryArgs" is consumed and freed by the main thread
    pthread_create(&vm->main_thread, NULL, ruby_vm_main_thread_func, transferredMemoryArgs);

    vm->vm_started = 1;
    return 0;
}

void ruby_vm_enqueue(RubyVM* vm, RubyScript* script, RubyCompletionTask on_complete) {
    RubyScriptEnqueueArgs* transferredMemoryArgs = malloc(sizeof(RubyScriptEnqueueArgs));
    transferredMemoryArgs->vm = vm;
    transferredMemoryArgs->script = script;
    transferredMemoryArgs->on_complete = on_complete;

    fprintf(stderr, "ENQUEUING SCRIPT\n");

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
    write(vm->commands_channel.main_fd , content, strlen(content));
    write(vm->commands_channel.main_fd, "\n", 1);

    // Read exit code from VM socket output
    if (read(vm->commands_channel.main_fd, &result, sizeof(result)) >= 0) {
        result = result - '0';
    } else {
        fprintf(stderr, "error while reading exit code socket\n");
    }

    // Now command is executed and return code queried, let the place to the next script
    pthread_mutex_unlock(&vm->socket_lock);

    ruby_completion_task_invoke(&on_complete, result);

    // Pop and execute cleanup handler
    // NOTE: argument '1' means EXECUTE the cleanup
    pthread_cleanup_pop(1);

    return NULL;
}
