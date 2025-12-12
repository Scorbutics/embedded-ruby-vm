#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>

#include "ruby-interpreter.h"
#include "ruby-script.h"
#include "install.h"
#include "assets-error.h"

/* Global log file pointer */
static FILE* g_log_file = NULL;
static volatile char finished = 0;
static void* embedded_ruby_handle = NULL;

static void OnScriptCompleted(void* context, int result) {
    (void)context;
    sleep(5); // Simulate some processing delay
    const char* msg = "Script completed with exit code: ";
    
    if (g_log_file != NULL) {
        fprintf(g_log_file, "%s%d\n", msg, result);
        fflush(g_log_file);
    }
    finished = 1;
}

static void OnLog(LogListener* listener, const char* line) {
    (void)listener;
    
    if (g_log_file != NULL) {
        fprintf(g_log_file, "[Ruby] %s\n", line);
        fflush(g_log_file);  /* Ensure immediate write */
    }
    
}

static void OnLogError(LogListener* listener, const char* line) {
    (void)listener;
    
    if (g_log_file != NULL) {
        fprintf(g_log_file, "[Ruby Error] %s\n", line);
        fflush(g_log_file);  /* Ensure immediate write */
    }
    
}

int main(int argc, char* argv[]) {
    int result = 0;
    RubyScript* script = NULL;
    RubyInterpreter* interpreter = NULL;
    const char* script_content = NULL;
    const char* log_file_path = "ruby_vm_test.log";
    AssetsError assets_error;
    AssetsLayout* layout = NULL;

    /* Configuration */
    const char* install_dir = "./test-ruby-install";  /* Where to extract assets */
    const char* ruby_base_dir = NULL;                 /* Will be set from layout */
    const char* execution_location = ".";             /* Working directory */
    const char* native_libs_dir = NULL;               /* Will be set from layout */

    /* Simple test script */
    const char* test_script =
        "puts 'Hello from Ruby!'\n"
        "puts \"Ruby version: #{RUBY_VERSION}\"\n"
        "puts '2 + 2 = ' + (2 + 2).to_s\n";

    /* Open log file */
    g_log_file = fopen(log_file_path, "w");
    if (g_log_file == NULL) {
        fprintf(stderr, "Warning: Cannot open log file '%s', logging to console only\n",
                log_file_path);
    } else {
        printf("Logging to file: %s\n", log_file_path);
    }

    /* ========================================================================
     * Bootstrap Ruby Runtime
     * This single call handles:
     *   1. Checking if extraction is needed
     *   2. Extracting embedded assets (Ruby stdlib + native libs)
     *   3. Getting the asset layout
     *   4. Loading native libraries in dependency order
     * ======================================================================== */
    printf("=== Bootstrapping Ruby Runtime ===\n");
    printf("Install directory: %s\n\n", install_dir);

    assets_error_init(&assets_error);
    layout = assets_bootstrap(install_dir, &assets_error);

    if (layout == NULL) {
        fprintf(stderr, "Bootstrap failed: %s\n", assets_error.message);
        if (assets_error.context[0] != '\0') {
            fprintf(stderr, "  Context: %s\n", assets_error.context);
        }
        if (g_log_file) {
            fprintf(g_log_file, "Bootstrap failed: %s\n", assets_error.message);
            fflush(g_log_file);
        }
        result = 10;
        goto cleanup;
    }

    ruby_base_dir = layout->ruby_stdlib_path;
    native_libs_dir = layout->native_libs_dir;

    printf("✓ Bootstrap complete\n");
    printf("  Ruby stdlib: %s\n", ruby_base_dir);
    printf("  Native libs: %s\n\n", native_libs_dir);

    /* ========================================================================
     * Preload Ruby dependencies before loading libembedded-ruby.so
     * Note: Setting LD_LIBRARY_PATH with setenv() doesn't affect dlopen() in
     * the current process since the dynamic linker reads it at startup only.
     * Solution: Explicitly load dependencies with absolute paths first.
     * ======================================================================== */
    printf("Preloading Ruby native dependencies...\n");
    
    // Define dependencies in load order (dependencies first)
    const char* deps[] = {
        "libembedded-ruby.so",
        NULL
    };
    
    // Load each dependency with absolute path and RTLD_GLOBAL
    for (int i = 0; deps[i] != NULL; i++) {
        char lib_path[2048];
        snprintf(lib_path, sizeof(lib_path), "%s/%s", native_libs_dir, deps[i]);
        
        void* handle = dlopen(lib_path, RTLD_NOW | RTLD_GLOBAL);
        if (handle) {
            printf("  ✓ Loaded: %s\n", deps[i]);
        } else {
            // Not a fatal error - library might not exist or already loaded
            printf("  ⚠ Skipped: %s (%s)\n", deps[i], dlerror());
            dlerror(); // Clear error
        }
    }
    printf("\n");

    /* ========================================================================
     * Load libembedded-ruby.so dynamically
     * Dependencies should now be available in the global symbol namespace
     * ======================================================================== */
    printf("Loading libembedded-ruby.so...\n");

    /* Try multiple paths to find libembedded-ruby.so */
    const char* lib_paths[] = {
        "libembedded-ruby.so",          /* Current dir or LD_LIBRARY_PATH */
        "../lib/libembedded-ruby.so",   /* Relative to bin/ */
        "./libembedded-ruby.so"         /* Current directory */
    };

    embedded_ruby_handle = NULL;
    for (size_t i = 0; i < sizeof(lib_paths) / sizeof(lib_paths[0]); i++) {
        embedded_ruby_handle = dlopen(lib_paths[i], RTLD_NOW | RTLD_GLOBAL);
        if (embedded_ruby_handle) {
            printf("✓ libembedded-ruby.so loaded from: %s\n\n", lib_paths[i]);
            break;
        }
    }

    if (!embedded_ruby_handle) {
        fprintf(stderr, "Failed to load libembedded-ruby.so. Tried:\n");
        for (size_t i = 0; i < sizeof(lib_paths) / sizeof(lib_paths[0]); i++) {
            fprintf(stderr, "  - %s\n", lib_paths[i]);
        }
        fprintf(stderr, "Error: %s\n", dlerror());
        if (g_log_file) {
            fprintf(g_log_file, "Failed to load libembedded-ruby.so: %s\n", dlerror());
            fflush(g_log_file);
        }
        result = 13;
        goto cleanup;
    }

    printf("=== Ruby VM Test ===\n");

    script_content = test_script;

    /* Setup logging */
    LogListener listener = {
        .context = NULL,
        .user_data = NULL,
        .accept = OnLog,
        .on_log_error = OnLogError
    };

    printf("Execution location: %s\n", execution_location);
    printf("Ruby base directory: %s\n", ruby_base_dir);
    printf("Native libs directory: %s\n\n", native_libs_dir);

    /* Create interpreter */
    printf("Initializing Ruby interpreter...\n");
    if (g_log_file != NULL) {
        fprintf(g_log_file, "Initializing Ruby interpreter...\n");
        fflush(g_log_file);
    }

    interpreter = ruby_interpreter_create(
        execution_location,
        ruby_base_dir,
        native_libs_dir,
        listener
    );

    if (interpreter == NULL) {
        const char* error_msg = "Error: Failed to create Ruby interpreter (out of memory)";
        fprintf(stderr, "%s\n", error_msg);
        if (g_log_file != NULL) {
            fprintf(g_log_file, "%s\n", error_msg);
            fflush(g_log_file);
        }
        result = 1;
        goto cleanup;
    }

    printf("Interpreter created successfully\n\n");

    /* Create script */
    printf("Loading Ruby script...\n");
    
    script = ruby_script_create_from_content(script_content, strlen(script_content));
    
    if (script == NULL) {
        const char* error_msg = "Error: Failed to create Ruby script";
        fprintf(stderr, "%s\n", error_msg);
        result = 2;
        goto cleanup;
    }
    
    printf("Script loaded successfully\n\n");

    /* Execute script */
    printf("=== Script Output ===\n");

    result = ruby_interpreter_enqueue(
        interpreter,
        script,
        ruby_completion_task_create(OnScriptCompleted, NULL)
    );

    if (result != 0) {
        const char* error_msg = "\nError: Script execution failed";
        fprintf(stderr, "%s with code %d\n", error_msg, result);

        // Get detailed error message from interpreter
        const char* detailed_error = ruby_interpreter_get_error_message(interpreter);
        if (detailed_error) {
            fprintf(stderr, "Details: %s\n", detailed_error);
            if (g_log_file != NULL) {
                fprintf(g_log_file, "%s with code %d\n", error_msg, result);
                fprintf(g_log_file, "Details: %s\n", detailed_error);
                fflush(g_log_file);
            }
        }

        goto cleanup;
    }
    
    while (!finished);

    ruby_script_destroy(script);

    printf("=== End of Output ===\n");

cleanup:
    /* Cleanup */
    if (interpreter != NULL) {
        ruby_interpreter_destroy(interpreter);
    }

    if (layout != NULL) {
        assets_free_layout(layout);
    }

    if (embedded_ruby_handle != NULL) {
        dlclose(embedded_ruby_handle);
        embedded_ruby_handle = NULL;
    }

    if (g_log_file != NULL) {
        fclose(g_log_file);
        g_log_file = NULL;
    }

    printf("\nTest completed with exit code: %d\n", result);

    return result;
}