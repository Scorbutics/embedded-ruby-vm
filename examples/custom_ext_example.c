/**
 * Example demonstrating custom Ruby extension callback mechanism
 * 
 * This example shows how to:
 * 1. Create a custom Ruby extension in C
 * 2. Register it via the callback mechanism
 * 3. Use it from Ruby code via require statement
 * 
 * Build with:
 *   gcc -o custom_ext_example custom_ext_example.c \
 *       -I../core/ruby-vm \
 *       -L../build/lib -lembedded-ruby -lassets \
 *       -lpthread -ldl
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Include Ruby API (static build for this example)
#include "ruby-api-loader.h"

// Ruby headers for extension development
#include "ruby/ruby.h"

// ============================================================================
// Custom Ruby Extension Implementation
// ============================================================================

/**
 * Example extension function: MyExtension.greet(name)
 * Returns a greeting string
 */
static VALUE my_extension_greet(VALUE self, VALUE name) {
    const char* name_str = StringValueCStr(name);
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "Hello, %s! This is from custom C extension.", name_str);
    return rb_str_new_cstr(buffer);
}

/**
 * Example extension function: MyExtension.add(a, b)
 * Returns the sum of two numbers
 */
static VALUE my_extension_add(VALUE self, VALUE a, VALUE b) {
    int result = NUM2INT(a) + NUM2INT(b);
    return INT2NUM(result);
}

/**
 * Extension initialization function
 * MUST be named Init_<extension_name> for Ruby to recognize it
 * 
 * This function is called during Ruby VM initialization to register
 * the extension's classes, modules, and methods.
 */
void Init_my_extension(void) {
    // Create a module named "MyExtension"
    VALUE module = rb_define_module("MyExtension");
    
    // Define module methods
    rb_define_module_function(module, "greet", my_extension_greet, 1);
    rb_define_module_function(module, "add", my_extension_add, 2);
    
    // You could also define constants, classes, etc.
    rb_define_const(module, "VERSION", rb_str_new_cstr("1.0.0"));
}

// ============================================================================
// Custom Extension Loader (Callback)
// ============================================================================

/**
 * This callback function is called by the Ruby VM during initialization.
 * It should call all your custom Init_* functions.
 */
void initialize_custom_extensions(void) {
    printf("[C] Initializing custom extensions...\n");
    
    // Register our extension
    Init_my_extension();
    
    // You can register multiple extensions here:
    // Init_another_extension();
    // Init_yet_another_extension();
    
    printf("[C] Custom extensions initialized!\n");
}

// ============================================================================
// Logging Support
// ============================================================================

static void log_accept(LogListener* listener, const char* message) {
    (void)listener;
    printf("[Ruby] %s\n", message);
}

static void log_error(LogListener* listener, const char* message) {
    (void)listener;
    fprintf(stderr, "[Ruby Error] %s\n", message);
}

// ============================================================================
// Main Program
// ============================================================================

int main(int argc, char** argv) {
    printf("=== Custom Ruby Extension Example ===\n\n");
    
    // Load the Ruby API (static build)
    RubyAPI api;
    if (ruby_api_load(NULL, &api) != 0) {
        fprintf(stderr, "Failed to load Ruby API\n");
        return 1;
    }
    
    // CRITICAL: Set up custom extension callback BEFORE creating interpreter
    printf("[C] Registering custom extension callback...\n");
    api.set_custom_ext_init(initialize_custom_extensions);
    
    // Set up logging
    LogListener listener = {
        .context = NULL,
        .user_data = NULL,
        .accept = log_accept,
        .on_log_error = log_error
    };
    
    // Create Ruby interpreter
    // The callback will be invoked during this call
    printf("[C] Creating Ruby interpreter...\n");
    RubyInterpreter* interpreter = api.interpreter.create(
        ".",           // Working directory
        "./ruby",      // Ruby stdlib directory
        "./lib",       // Native libs directory
        listener
    );
    
    if (!interpreter) {
        fprintf(stderr, "Failed to create Ruby interpreter\n");
        ruby_api_unload(&api);
        return 1;
    }
    
    printf("\n=== Running Ruby Script ===\n\n");
    
    // Create a Ruby script that uses our custom extension
    const char* script_content = 
        "puts 'Ruby script starting...'\n"
        "puts ''\n"
        "\n"
        "# Require our custom extension (statically linked!)\n"
        "require 'my_extension'\n"
        "puts 'Successfully required my_extension!'\n"
        "puts ''\n"
        "\n"
        "# Use the extension\n"
        "puts MyExtension.greet('World')\n"
        "puts MyExtension.greet('Ruby Developer')\n"
        "puts ''\n"
        "\n"
        "result = MyExtension.add(42, 13)\n"
        "puts \"42 + 13 = #{result}\"\n"
        "puts ''\n"
        "\n"
        "puts \"Extension version: #{MyExtension::VERSION}\"\n"
        "puts ''\n"
        "puts 'Script completed successfully!'\n";
    
    RubyScript* script = api.script.create_from_content(
        script_content,
        strlen(script_content)
    );
    
    if (!script) {
        fprintf(stderr, "Failed to create script\n");
        api.interpreter.destroy(interpreter);
        ruby_api_unload(&api);
        return 1;
    }
    
    // Execute the script synchronously
    int exit_code = api.interpreter.execute_sync(interpreter, script);
    
    printf("\n=== Execution Complete ===\n");
    printf("Exit code: %d\n", exit_code);
    
    // Cleanup
    api.script.destroy(script);
    api.interpreter.destroy(interpreter);
    ruby_api_unload(&api);
    
    return exit_code;
}
