#ifndef RUBY_API_LOADER_H
#define RUBY_API_LOADER_H

#include <stddef.h>

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations - opaque types (library users don't need internal structure)
typedef struct RubyInterpreter RubyInterpreter;
typedef struct RubyScript RubyScript;

// Required type definitions for function pointers
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

/**
 * Helper to create a completion task.
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
 * Safe to call even if callback is NULL.
 * @param task The task to invoke
 * @param result The completion result code
 */
static inline void ruby_completion_task_invoke(RubyCompletionTask* task, int result) {
    if (task && task->callback) {
        task->callback(task->user_data, result);
    }
}

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
 * Load the Ruby API from libembedded-ruby.so
 *
 * @param lib_path Path to libembedded-ruby.so
 * @param api Pointer to RubyAPI struct to fill
 * @return 0 on success, -1 on error
 *
 * Usage:
 *   RubyAPI api;
 *   if (ruby_api_load("./libembedded-ruby.so", &api) != 0) {
 *       // handle error
 *   }
 *
 *   // Use the API
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
}

/**
 * Unload the Ruby API
 */
static inline void ruby_api_unload(RubyAPI* api) {
    if (api && api->handle) {
        dlclose(api->handle);
        memset(api, 0, sizeof(RubyAPI));
    }
}


// TODO in case we want a static build with static loading 
// we could also fill this RubyAPI structure with the real linked function
// and create a dedicated ruby-api-loader.c containing the private internal includes
// This way we still expose to public only this header file in all cases.

#ifdef __cplusplus
}
#endif

#endif /* RUBY_API_LOADER_H */
