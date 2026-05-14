#ifndef RUBY_API_LOADER_STATIC_H
#define RUBY_API_LOADER_STATIC_H

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

/* Static build - include actual function declarations */
#include "embedded-ruby-vm/ruby-interpreter.h"
#include "embedded-ruby-vm/ruby-script.h"

/**
 * Custom Ruby Extension Callback API
 * Allows external projects to register statically-linked Ruby extensions
 * that will be resolvable via require statements.
 */

/**
 * Function pointer type for custom Ruby extension initialization.
 * This callback is invoked after Init_ext() and before Ruby starts executing scripts.
 */
typedef void (*RubyCustomExtInit)(void);

/**
 * Set the custom extension initialization callback.
 * Must be called BEFORE creating the Ruby interpreter.
 * 
 * @param init_func Function pointer to your custom extension init, or NULL to clear
 */
void ruby_set_custom_ext_init(RubyCustomExtInit init_func);


/**
 * Struct containing all Ruby API function pointers.
 * In static builds, these point to statically-linked functions.
 */
typedef struct {
    RubyInterpreter* (*create)(const char*, const char*, const char*, LogListener);
    void (*destroy)(RubyInterpreter*);
    int (*enqueue)(RubyInterpreter*, RubyScript*, RubyCompletionTask);
    int (*execute_sync)(RubyInterpreter*, RubyScript*);
    int (*enable_logging)(RubyInterpreter*);
    int (*disable_logging)(RubyInterpreter*);
    int (*enable_remote_debug)(RubyInterpreter*, const RubyVMRemoteDebugOptions*);
    int (*enable_remote_eval)(RubyInterpreter*, const RubyVMRemoteEvalOptions*);
    const char* (*get_error_message)(const RubyInterpreter*);
} RubyInterpreterAPI;

typedef struct {
    RubyScript* (*create_from_content)(const char*, size_t);
    void (*destroy)(RubyScript*);
} RubyScriptAPI;

typedef struct {
    void* handle;  /* Always NULL for static builds */
    RubyInterpreterAPI interpreter;
    RubyScriptAPI script;
    void (*set_custom_ext_init)(RubyCustomExtInit);  /* Custom extension callback setter */
} RubyAPI;

/**
 * Load library dependencies from a .deps file
 *
 * The .deps file format contains C-style string entries like:
 *     "libruby.so.3.1",
 *     "libssl.so",
 *
 * This function parses the file and loads each library from the specified directory.
 *
 * Note: This function is provided for compatibility with code that uses both static
 * and dynamic builds. In pure static builds, this function is typically not needed,
 * but it's useful for test code that demonstrates dynamic loading.
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
 * Load the Ruby API from statically-linked functions.
 *
 * @param lib_path Ignored in static builds (can be NULL)
 * @param api Pointer to RubyAPI struct to fill
 * @return 0 on success, -1 on error
 *
 * Usage:
 *   RubyAPI api;
 *   if (ruby_api_load(NULL, &api) != 0) {
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

    /* Unused in static build */
    (void)lib_path;

    memset(api, 0, sizeof(RubyAPI));

    /* No handle for static linking */
    api->handle = NULL;

    /* Assign interpreter functions */
    api->interpreter.create = ruby_interpreter_create;
    api->interpreter.destroy = ruby_interpreter_destroy;
    api->interpreter.enqueue = ruby_interpreter_enqueue;
    api->interpreter.execute_sync = ruby_interpreter_execute_sync;
    api->interpreter.enable_logging = ruby_interpreter_enable_logging;
    api->interpreter.disable_logging = ruby_interpreter_disable_logging;
    api->interpreter.enable_remote_debug = ruby_interpreter_enable_remote_debug;
    api->interpreter.enable_remote_eval = ruby_interpreter_enable_remote_eval;
    api->interpreter.get_error_message = ruby_interpreter_get_error_message;

    /* Assign script functions */
    api->script.create_from_content = ruby_script_create_from_content;
    api->script.destroy = ruby_script_destroy;

    /* Assign custom extension callback setter */
    api->set_custom_ext_init = ruby_set_custom_ext_init;

    return 0;
}

/**
 * Unload the Ruby API (no-op for static builds).
 */
static inline void ruby_api_unload(RubyAPI* api) {
    if (api) {
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
 * Note: In static builds, this function is provided for compatibility with code
 * that uses both static and dynamic builds. It's particularly useful for test code
 * that demonstrates dynamic loading even when the main build is static.
 *
 * @param api Pointer to RubyAPI struct to fill
 * @param deps_file_paths Array of possible paths to the .deps file (NULL-terminated)
 * @param lib_paths Array of possible paths to the main library (NULL-terminated)
 * @param native_libs_dir Directory containing the native libraries
 * @return 0 on success, -1 on error
 *
 */
static inline int ruby_api_bootstrap(
    RubyAPI* api,
    const char** deps_file_paths,
    const char** lib_paths,
    const char* native_libs_dir
) {
    (void) deps_file_paths;  /* Unused in static build */
    (void) native_libs_dir;  /* Unused in static build */
    (void) lib_paths;        /* Unused in static build */
    if (!api) {
        fprintf(stderr, "ruby_api_bootstrap: api parameter is NULL\n");
        return -1;
    }  

    if (ruby_api_load(NULL, api) != 0) {
        fprintf(stderr, "Failed to load internal embedded ruby.\n");
        return -1;
    }

    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* RUBY_API_LOADER_STATIC_H */
