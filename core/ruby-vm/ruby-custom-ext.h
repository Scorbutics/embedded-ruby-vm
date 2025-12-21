#ifndef RUBY_CUSTOM_EXT_H
#define RUBY_CUSTOM_EXT_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file ruby-custom-ext.h
 * @brief Custom Ruby Extension Callback API
 * 
 * This header provides a callback mechanism for registering custom Ruby extensions
 * that will be statically resolved by the Ruby interpreter. This allows external
 * projects to integrate their own C extensions without modifying the embedded-ruby-vm
 * repository.
 * 
 * @example
 * ```c
 * // Define your custom extension
 * void Init_my_extension(void) {
 *     VALUE mod = rb_define_module("MyExtension");
 *     rb_define_module_function(mod, "hello", my_hello, 0);
 * }
 * 
 * // Callback function that registers all your extensions
 * void my_custom_extensions_init(void) {
 *     Init_my_extension();
 *     // Add more Init_* calls as needed
 * }
 * 
 * // Before creating the Ruby interpreter
 * ruby_set_custom_ext_init(my_custom_extensions_init);
 * 
 * // Now Ruby code can use: require 'my_extension'
 * ```
 */

/**
 * @typedef RubyCustomExtInit
 * @brief Function pointer type for custom Ruby extension initialization.
 * 
 * This callback is invoked after Init_ext() (which initializes Ruby's built-in
 * statically-linked extensions) and before Ruby starts executing user scripts.
 * 
 * Use this to call your custom Init_* functions to register Ruby extensions
 * that will be statically available (resolvable via require statements).
 * 
 * @note The callback is executed in the Ruby VM thread context.
 * @note Multiple extensions can be registered in a single callback by calling
 *       their respective Init_* functions sequentially.
 */
typedef void (*RubyCustomExtInit)(void);

/**
 * @brief Set the custom extension initialization callback.
 * 
 * Registers a callback function that will be called during Ruby VM initialization
 * to register custom statically-linked extensions. This must be called BEFORE
 * creating the Ruby interpreter (before ruby_interpreter_create()).
 * 
 * @param init_func Function pointer to your custom extension initialization function,
 *                  or NULL to clear the callback.
 * 
 * @note Only one callback can be registered at a time. Calling this function
 *       multiple times will replace the previous callback.
 * @note The callback is optional - if not set, only Ruby's built-in extensions
 *       will be available.
 * @note Thread safety: This function should be called from the main thread before
 *       any Ruby interpreters are created.
 * 
 * @example
 * ```c
 * // Set the callback before creating interpreter
 * ruby_set_custom_ext_init(my_custom_extensions_init);
 * 
 * // Create interpreter (callback will be invoked during initialization)
 * RubyInterpreter* interp = ruby_interpreter_create(...);
 * 
 * // Clear the callback (optional)
 * ruby_set_custom_ext_init(NULL);
 * ```
 */
void ruby_set_custom_ext_init(RubyCustomExtInit init_func);

#ifdef __cplusplus
}
#endif

#endif // RUBY_CUSTOM_EXT_H
