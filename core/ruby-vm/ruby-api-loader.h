#ifndef RUBY_API_LOADER_H
#define RUBY_API_LOADER_H

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifndef RUBY_STATIC
#include <dlfcn.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations - opaque types (library users don't need internal structure)
typedef struct RubyInterpreter RubyInterpreter;
typedef struct RubyScript RubyScript;

#ifdef RUBY_STATIC
/* Static build - include actual function declarations */
#include "ruby-interpreter.h"
#include "ruby-script.h"
#else
/* Dynamic build - manually define types needed for API structure */
/* (In static build, these are already defined in log-listener.h and completion-task.h) */

typedef struct LogListener LogListener;
typedef void (*LogAcceptFunc)(struct LogListener* listener, const char* lineMessage);
typedef void (*LogErrorFunc)(struct LogListener* listener, const char* errorMessage);

struct LogListener {
    void* context;
    void* user_data;
    LogAcceptFunc accept;
    LogErrorFunc on_log_error;
};

typedef void (*RubyCompletionCallback)(void* user_data, int result);

typedef struct {
    RubyCompletionCallback callback;
    void* user_data;
} RubyCompletionTask;
#endif

/* Helper functions (available in both static and dynamic builds) */
#ifndef RUBY_STATIC
/**
 * Helper to create a completion task.
 * In static builds, this is already defined in completion-task.h
 * @param callback Function to call on completion (can be NULL)
 * @param user_data Context data to pass to callback (can be NULL)
 * @return Initialized RubyCompletionTask
 */
static inline RubyCompletionTask ruby_completion_task_create(
        RubyCompletionCallback callback,
        void* user_data
) {
    RubyCompletionTask task = {
            .callback = callback,
            .user_data = user_data
    };
    return task;
}

/**
 * Helper to invoke a completion task.
 * In static builds, this is already defined in completion-task.h
 * Safe to call even if callback is NULL.
 * @param task The task to invoke
 * @param result The completion result code
 */
static inline void ruby_completion_task_invoke(RubyCompletionTask* task, int result) {
    if (task && task->callback) {
        task->callback(task->user_data, result);
    }
}
#endif

/**
 * Struct containing all dynamically loaded Ruby API function pointers.
 * This approach avoids symbol conflicts and makes it clear that functions
 * are loaded dynamically.
 */
typedef struct {
    RubyInterpreter* (*create)(const char*, const char*, const char*, LogListener);
    void (*destroy)(RubyInterpreter*);
    int (*enqueue)(RubyInterpreter*, RubyScript*, RubyCompletionTask);
    int (*execute_sync)(RubyInterpreter*, RubyScript*);
    int (*enable_logging)(RubyInterpreter*);
    int (*disable_logging)(RubyInterpreter*);
    const char* (*get_error_message)(const RubyInterpreter*);
} RubyInterpreterAPI;

typedef struct {
    RubyScript* (*create_from_content)(const char*, size_t);
    void (*destroy)(RubyScript*);
} RubyScriptAPI;

typedef struct {
    void* handle;
    RubyInterpreterAPI interpreter;
    RubyScriptAPI script;
} RubyAPI;

#define LOAD_SYM(handle, api_ptr, name) \
    do { \
        *(void**)&(api_ptr) = dlsym(handle, name); \
        if (!(api_ptr)) { \
            fprintf(stderr, "Failed to load symbol '%s': %s\n", name, dlerror()); \
            dlclose(handle); \
            return -1; \
        } \
    } while(0)

/**
 * Load the Ruby API from libembedded-ruby.so (dynamic) or statically-linked functions.
 *
 * @param lib_path Path to libembedded-ruby.so (ignored if RUBY_STATIC is defined)
 * @param api Pointer to RubyAPI struct to fill
 * @return 0 on success, -1 on error
 *
 * Usage (dynamic build):
 *   RubyAPI api;
 *   if (ruby_api_load("./libembedded-ruby.so", &api) != 0) {
 *       // handle error
 *   }
 *
 * Usage (static build with -DRUBY_STATIC):
 *   RubyAPI api;
 *   if (ruby_api_load(NULL, &api) != 0) {
 *       // handle error
 *   }
 *
 *   // Use the API (same for both):
 *   RubyInterpreter* interp = api.interpreter.create(...);
 *   api.interpreter.destroy(interp);
 *
 *   // Cleanup
 *   ruby_api_unload(&api);
 */
static inline int ruby_api_load(const char* lib_path, RubyAPI* api) {
    if (!api) {
        fprintf(stderr, "ruby_api_load: api parameter is NULL\n");
        return -1;
    }

    memset(api, 0, sizeof(RubyAPI));

#ifdef RUBY_STATIC
    /* Static build - directly assign statically-linked function pointers */
    (void)lib_path; /* Unused in static build */

    /* No handle for static linking */
    api->handle = NULL;

    /* Assign interpreter functions */
    api->interpreter.create = ruby_interpreter_create;
    api->interpreter.destroy = ruby_interpreter_destroy;
    api->interpreter.enqueue = ruby_interpreter_enqueue;
    api->interpreter.execute_sync = ruby_interpreter_execute_sync;
    api->interpreter.enable_logging = ruby_interpreter_enable_logging;
    api->interpreter.disable_logging = ruby_interpreter_disable_logging;
    api->interpreter.get_error_message = ruby_interpreter_get_error_message;

    /* Assign script functions */
    api->script.create_from_content = ruby_script_create_from_content;
    api->script.destroy = ruby_script_destroy;

    return 0;
#else
    /* Dynamic build - load library via dlopen */

    /* Clear any existing dlopen error */
    dlerror();

    /* Load the library */
    api->handle = dlopen(lib_path, RTLD_NOW | RTLD_GLOBAL);
    if (!api->handle) {
        fprintf(stderr, "Failed to load %s: %s\n", lib_path ? lib_path : "libembedded-ruby.so", dlerror());
        return -1;
    }

    /* Load interpreter functions */
    LOAD_SYM(api->handle, api->interpreter.create, "ruby_interpreter_create");
    LOAD_SYM(api->handle, api->interpreter.destroy, "ruby_interpreter_destroy");
    LOAD_SYM(api->handle, api->interpreter.enqueue, "ruby_interpreter_enqueue");
    LOAD_SYM(api->handle, api->interpreter.execute_sync, "ruby_interpreter_execute_sync");
    LOAD_SYM(api->handle, api->interpreter.enable_logging, "ruby_interpreter_enable_logging");
    LOAD_SYM(api->handle, api->interpreter.disable_logging, "ruby_interpreter_disable_logging");
    LOAD_SYM(api->handle, api->interpreter.get_error_message, "ruby_interpreter_get_error_message");

    /* Load script functions */
    LOAD_SYM(api->handle, api->script.create_from_content, "ruby_script_create_from_content");
    LOAD_SYM(api->handle, api->script.destroy, "ruby_script_destroy");

    return 0;
#endif
}

/**
 * Unload the Ruby API
 */
static inline void ruby_api_unload(RubyAPI* api) {
    if (api && api->handle) {
#ifndef RUBY_STATIC
        dlclose(api->handle);
#endif
        memset(api, 0, sizeof(RubyAPI));
    }
}

#ifdef __cplusplus
}
#endif

#endif /* RUBY_API_LOADER_H */
