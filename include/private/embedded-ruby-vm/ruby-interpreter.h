#ifndef INTERPRETER_H
#define INTERPRETER_H

#include "embedded-ruby-vm/ruby-api.h"
#include "embedded-ruby-vm/ruby-script-location.h"
#include "embedded-ruby-vm/ruby-vm.h"  // for RubyVMRemoteDebugOptions

#ifdef __cplusplus
extern "C" {
#endif

struct RubyVM;
struct RubyScript;

typedef struct RubyVM RubyVM;

struct RubyInterpreter {
    char* application_path;
    char* ruby_base_directory;
    char* native_libs_location;
    RubyVM* vm;
    LogListener log_listener;
    /* Process-unique id assigned at ruby_interpreter_create. Threaded down
     * into ruby_vm_execute_sync / ruby_vm_enqueue and prepended to every
     * request sent over the FIFO command channel. The Ruby-side dispatcher
     * routes each script to a per-id worker Thread, and tags every
     * response with the same id so the C-side reader thread can demultiplex
     * exit codes back to the right waiter. */
    int interpreter_id;
};

#ifdef __cplusplus
}
#endif

#endif
