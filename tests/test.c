#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ruby-interpreter.h"
#include "ruby-script.h"

/* Global log file pointer */
static FILE* g_log_file = NULL;
static volatile char finished = 0;

static void OnScriptCompleted(void* context, int result) {
    (void)context;
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

    /* Configuration */
    const char* ruby_base_dir = "./ruby";           /* Ruby standard library location */
    const char* execution_location = ".";           /* Working directory */
    const char* native_libs_dir = "./lib";          /* Native extensions location */
    
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

    script_content = test_script;

    /* Setup logging */
    LogListener listener = {
        .context = NULL,
        .user_data = NULL,
        .accept = OnLog,
        .on_log_error = OnLogError
    };

    printf("=== Embedded Ruby VM Test ===\n");
    printf("Ruby base directory: %s\n", ruby_base_dir);
    printf("Execution location: %s\n", execution_location);
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
        const char* error_msg = "Error: Failed to create Ruby interpreter";
        fprintf(stderr, "%s\n", error_msg);
        if (g_log_file != NULL) {
            fprintf(g_log_file, "%s\n", error_msg);
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
        const char* error_msg = "\nError: Script execution failed with code";
        fprintf(stderr, "%s %d\n", error_msg, result);
        goto cleanup;
    }
    
    while (!finished);

    sleep(2);
    printf("=== End of Output ===\n");

cleanup:
    /* Cleanup */
    if (interpreter != NULL) {
        ruby_interpreter_destroy(interpreter);
    }
    
    /* Free dynamically allocated script content if loaded from file */
    if (argc > 1 && script_content != test_script) {
        free((void*)script_content);
    }

    printf("\nTest completed with exit code: %d\n", result);

    return result;
}