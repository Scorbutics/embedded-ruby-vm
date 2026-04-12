#ifndef RUBY_API_LOADER_SHARED_H
#define RUBY_API_LOADER_SHARED_H

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations - opaque types (library users don't need internal structure)
typedef struct RubyInterpreter RubyInterpreter;
typedef struct RubyScript RubyScript;

/* LogListener struct, log_stream_t enum, and callback typedefs */
#include "embedded-ruby-vm/log-listener.h"

typedef void (*RubyCompletionCallback)(void* user_data, int result);

typedef struct {
    RubyCompletionCallback callback;
    void* user_data;
} RubyCompletionTask;

/**
 * Custom Ruby Extension Callback API
 * Allows external projects to register statically-linked Ruby extensions
 * that will be resolvable via require statements.
 */

/**
 * Function pointer type for custom Ruby extension initialization.
 * This callback is invoked after Init_ext() and before Ruby starts executing scripts.
 * 
 * Note: In dynamic builds, this function is loaded from the shared library.
 * Use ruby_api_load() or ruby_api_bootstrap() to ensure it's available.
 */
typedef void (*RubyCustomExtInit)(void);

/**
 * Set the custom extension initialization callback.
 * Must be called BEFORE creating the Ruby interpreter.
 * 
 * @param init_func Function pointer to your custom extension init, or NULL to clear
 * 
 * Note: This is a standalone function exported by libembedded-ruby.so.
 * It's not part of the RubyAPI struct because it must be called before
 * the interpreter is created.
 */
typedef void (*RubySetCustomExtInitFunc)(RubyCustomExtInit init_func);


/* Helper functions for dynamic builds */

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
    RubySetCustomExtInitFunc set_custom_ext_init;  /* Custom extension callback setter */
} RubyAPI;

#define LOAD_SYM(handle, api_ptr, name) \
    do { \
        *(void**)&(api_ptr) = dlsym(handle, name); \
        if (!(api_ptr)) { \
            fprintf(stderr, "Failed to load symbol '%s': %s\n", name, dlerror()); \
            dlclose(handle); \
            memset(api, 0, sizeof(RubyAPI)); \
            return -1; \
        } \
    } while(0)

/**
 * Load library dependencies from a .deps file
 *
 * The .deps file format contains C-style string entries like:
 *     "libruby.so.3.1",
 *     "libssl.so",
 *
 * This function parses the file and loads each library from the specified directory.
 *
 * @param deps_file Path to the .deps file (e.g., "libembedded-ruby.deps")
 * @param libs_dir Directory containing the libraries
 * @return 0 on success, -1 on error
 */
static inline int load_dependencies_from_file(const char* deps_file, const char* libs_dir) {
    FILE* f = fopen(deps_file, "r");
    if (!f) {
        fprintf(stderr, "Cannot open deps file: %s\n", deps_file);
        return -1;
    }

    char line[512];
    int loaded_count = 0;
    int failed_count = 0;

    while (fgets(line, sizeof(line), f)) {
        // Skip empty lines and comments
        if (line[0] == '\n' || line[0] == '#' || line[0] == '/') {
            continue;
        }

        // Extract library name from: "libname.so",
        char* start = strchr(line, '"');
        if (!start) continue;
        start++; // Skip opening quote

        char* end = strchr(start, '"');
        if (!end) continue;
        *end = '\0'; // Null-terminate

        // Build full path
        char lib_path[1024];
        snprintf(lib_path, sizeof(lib_path), "%s/%s", libs_dir, start);

        // Load with RTLD_NOW | RTLD_GLOBAL
        void* handle = dlopen(lib_path, RTLD_NOW | RTLD_GLOBAL);
        if (handle) {
            printf("  ✓ Loaded: %s\n", start);
            loaded_count++;
        } else {
            // Not necessarily fatal - might be a system library
            const char* error = dlerror();
            printf("  ⚠ Skipped: %s (%s)\n", start, error ? error : "unknown error");
            failed_count++;
            dlerror(); // Clear error
        }
    }

    fclose(f);

    printf("Dependency loading complete: %d loaded, %d skipped\n", loaded_count, failed_count);
    return 0;
}

/**
 * Load the Ruby API from libembedded-ruby.so (dynamic loading).
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
 *   // Use the API:
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

    /* Load custom extension callback setter */
    LOAD_SYM(api->handle, api->set_custom_ext_init, "ruby_set_custom_ext_init");

    return 0;
}

/**
 * Unload the Ruby API and close the shared library.
 */
static inline void ruby_api_unload(RubyAPI* api) {
    if (api && api->handle) {
        dlclose(api->handle);
        memset(api, 0, sizeof(RubyAPI));
    }
}

/**
 * Bootstrap the Ruby API by loading dependencies and the main library.
 *
 * This is a convenience function that:
 * 1. Attempts to load dependencies from multiple possible .deps file locations
 * 2. Attempts to load the main library from multiple possible locations
 * 3. Returns 0 on success, -1 on failure
 *
 * @param api Pointer to RubyAPI struct to fill
 * @param deps_file_paths Array of possible paths to the .deps file (NULL-terminated)
 * @param lib_paths Array of possible paths to the main library (NULL-terminated)
 * @param native_libs_dir Directory containing the native libraries
 * @return 0 on success, -1 on error
 *
 * Usage:
 *   const char* deps_paths[] = {
 *       "../lib/libembedded-ruby.deps",
 *       "./libembedded-ruby.deps",
 *       NULL
 *   };
 *   const char* lib_paths[] = {
 *       "../lib/libembedded-ruby.so",
 *       "./libembedded-ruby.so",
 *       NULL
 *   };
 *
 *   RubyAPI api;
 *   if (ruby_api_bootstrap(&api, deps_paths, lib_paths, native_libs_dir) != 0) {
 *       // handle error
 *   }
 */
static inline int ruby_api_bootstrap(
    RubyAPI* api,
    const char** deps_file_paths,
    const char** lib_paths,
    const char* native_libs_dir
) {
    if (!api) {
        fprintf(stderr, "ruby_api_bootstrap: api parameter is NULL\n");
        return -1;
    }

    /* ========================================================================
     * Load dependencies from .deps file
     * ======================================================================== */
    int deps_loaded = 0;
    if (deps_file_paths) {
        for (const char** path = deps_file_paths; *path != NULL; path++) {
            if (load_dependencies_from_file(*path, native_libs_dir) == 0) {
                deps_loaded = 1;
                break;
            }
        }
    }

    /* ========================================================================
     * Load libembedded-ruby.so and resolve all API functions
     * ======================================================================== */
    if (!lib_paths) {
        fprintf(stderr, "ruby_api_bootstrap: lib_paths parameter is NULL\n");
        return -1;
    }

    int loaded = 0;
    for (const char** path = lib_paths; *path != NULL; path++) {
        if (ruby_api_load(*path, api) == 0) {
            loaded = 1;
            break;
        }
    }

    if (!loaded) {
        fprintf(stderr, "Failed to load libembedded-ruby.so. Tried:\n");
        for (const char** path = lib_paths; *path != NULL; path++) {
            fprintf(stderr, "  - %s\n", *path);
        }
        return -1;
    }

    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* RUBY_API_LOADER_SHARED_H */
