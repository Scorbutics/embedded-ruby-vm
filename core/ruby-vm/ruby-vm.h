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
    RubyVMError last_error;  // Last error that occurred
};
typedef struct RubyVM RubyVM;

RubyVM* ruby_vm_create(const char* application_path, RubyScript* main_script, LogListener listener);
void ruby_vm_destroy(RubyVM* vm);
int ruby_vm_start(RubyVM* vm, const char* ruby_base_directory, const char* native_libs_location);
int ruby_vm_enable_logging(RubyVM* vm);  // Optional: enable stdout/stderr redirection
void ruby_vm_enqueue(RubyVM* vm, RubyScript* script, RubyCompletionTask on_complete);

// Error handling
const RubyVMError* ruby_vm_get_last_error(const RubyVM* vm);
void ruby_vm_clear_error(RubyVM* vm);
const char* ruby_vm_get_error_message(const RubyVM* vm);

// Thread function prototypes
void* ruby_vm_main_thread_func(void* arg);
void* ruby_vm_log_reader_thread_func(void* arg);
void* ruby_vm_script_thread_func(void* arg);

// Helper structures for thread communication
typedef struct {
    RubyVM* vm;
    char* ruby_base_directory;
    char* native_libs_location;
} RubyVMStartArgs;

typedef struct {
    RubyVM* vm;
    RubyScript* script;
    RubyCompletionTask on_complete;
} RubyScriptEnqueueArgs;

#ifdef __cplusplus
}
#endif

#endif // RUBY_VM_H
