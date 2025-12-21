# Ruby VM Core Module

This directory contains the core embedded Ruby VM implementation, including the Ruby interpreter wrapper, script management, and communication infrastructure.

## Overview

The Ruby VM core provides:
- **Ruby Interpreter Lifecycle**: Creation, execution, and cleanup
- **Script Execution**: Synchronous and asynchronous script execution
- **Communication Channel**: Unix domain socket-based communication with Ruby
- **Logging Integration**: Capture Ruby stdout/stderr via callbacks
- **Asset Management**: Embedded Ruby stdlib extraction and loading

## Custom Ruby Extension Callback Mechanism

### What is it?

The custom extension callback mechanism allows **external projects** to register their own C extensions that will be statically linked and resolvable via Ruby's `require` statement. This enables you to:

- ✅ Create custom Ruby extensions in C/C++
- ✅ Link them statically into your application
- ✅ Make them available to Ruby scripts via `require`
- ✅ Keep the embedded-ruby-vm repository generic and reusable

### When to use it?

Use the custom extension callback when you need to:
- Integrate C/C++ libraries with Ruby (e.g., game engines, databases, native APIs)
- Provide platform-specific functionality to Ruby scripts
- Create domain-specific Ruby APIs backed by native code
- Extend Ruby's capabilities without modifying this repository

### How to use it

#### 1. Define Your Ruby Extension

Create a C file with your Ruby extension initialization:

```c
#include "ruby/ruby.h"

// Example: Custom database extension
static VALUE my_database_query(VALUE self, VALUE sql) {
    const char* query_str = StringValueCStr(sql);
    // Your native implementation here
    printf("Executing: %s\n", query_str);
    return rb_str_new_cstr("Result from native code");
}

// Extension initialization function (must be named Init_<extension_name>)
void Init_my_database(void) {
    VALUE module = rb_define_module("MyDatabase");
    rb_define_module_function(module, "query", my_database_query, 1);
}
```

#### 2. Create Extension Loader

Create a loader that registers all your custom extensions:

```c
#include "ruby-custom-ext.h"

// Forward declarations of your Init functions
extern void Init_my_database(void);
extern void Init_my_game_engine(void);
// Add more as needed...

// Callback function that registers all extensions
void initialize_my_custom_extensions(void) {
    Init_my_database();
    Init_my_game_engine();
    // Call more Init_* functions as needed
}
```

#### 3. Register the Callback

Before creating the Ruby interpreter, register your callback:

```c
#include "ruby-interpreter.h"
#include "ruby-custom-ext.h"

extern void initialize_my_custom_extensions(void);

int main(void) {
    // CRITICAL: Set callback BEFORE creating interpreter
    ruby_set_custom_ext_init(initialize_my_custom_extensions);
    
    // Create interpreter (your extensions will be initialized automatically)
    LogListener listener = {/* ... */};
    RubyInterpreter* interp = ruby_interpreter_create(
        ".",
        "./ruby",
        "./lib",
        listener
    );
    
    // Now your extensions are available in Ruby!
    RubyScript* script = ruby_script_create_from_content(
        "require 'my_database'\n"
        "puts MyDatabase.query('SELECT * FROM users')\n",
        /* length */
    );
    
    ruby_interpreter_execute_sync(interp, script, 10);
    
    // Cleanup
    ruby_script_destroy(script);
    ruby_interpreter_destroy(interp);
    return 0;
}
```

#### 4. Build Your Project

Link your extension and the embedded-ruby library together:

```cmake
add_executable(my_app
    src/main.c
    src/my_extension.c
    src/extension_loader.c
)

target_link_libraries(my_app
    PRIVATE
    embedded-ruby  # From this repository
    assets
)
```

#### 5. Use in Ruby

Your extensions are now available via standard Ruby `require`:

```ruby
require 'my_database'
require 'my_game_engine'

result = MyDatabase.query("SELECT * FROM users")
puts result

GameEngine.initialize
GameEngine.run_loop
```

### API Reference

#### `ruby-custom-ext.h`

```c
typedef void (*RubyCustomExtInit)(void);
void ruby_set_custom_ext_init(RubyCustomExtInit init_func);
```

**`RubyCustomExtInit`**
- Function pointer type for custom extension initialization callback
- Called after `Init_ext()` (Ruby's built-in extensions) and before script execution
- Should call all your custom `Init_*` functions

**`ruby_set_custom_ext_init(init_func)`**
- Registers the custom extension callback
- Must be called **before** creating the Ruby interpreter
- Pass `NULL` to clear the callback
- Only one callback can be registered at a time

### Execution Flow

```
1. Application starts
2. ruby_set_custom_ext_init(my_callback)  ← Register your callback
3. ruby_interpreter_create(...)            ← Create interpreter
   ├─ ruby_init()                          ← Initialize Ruby runtime
   ├─ Init_ext()                           ← Initialize Ruby's built-in extensions
   ├─ my_callback()                        ← **YOUR CALLBACK CALLED HERE**
   │  ├─ Init_my_database()                ← Your extensions registered
   │  └─ Init_my_game_engine()
   └─ ruby_run_node()                      ← Start executing Ruby code
4. Ruby scripts can now: require 'my_database'
```

### Best Practices

1. **Name your Init functions correctly**: Must be `Init_<extension_name>` (Ruby convention)
2. **Set callback early**: Call `ruby_set_custom_ext_init()` before creating the interpreter
3. **One callback per application**: All extensions should be registered in a single callback
4. **Thread safety**: Set the callback from the main thread before any threading
5. **Error handling**: Use Ruby's error handling APIs (`rb_raise`, etc.) in your extensions

### Example Projects

For complete working examples of using custom extensions, see:
- [Your external project structure example from the plan]
- Extension integration patterns
- Kotlin/JNI integration with custom extensions

### Troubleshooting

**Problem**: Ruby says "cannot load such file -- my_extension"
- **Solution**: Ensure your `Init_my_extension()` function is called in the callback
- **Check**: The Init function name matches the require name (underscores matter!)

**Problem**: Callback is not being called
- **Solution**: Verify `ruby_set_custom_ext_init()` is called **before** `ruby_interpreter_create()`
- **Check**: The callback pointer is not NULL

**Problem**: Segmentation fault during extension initialization
- **Solution**: Ensure Ruby is fully initialized before calling Ruby APIs in your Init function
- **Avoid**: Calling Ruby APIs outside of the Init function or before ruby_init()

## Public API Headers

When building projects that use this library, include:

- `ruby-interpreter.h` - Main interpreter API
- `ruby-script.h` - Script creation and management
- `ruby-vm-error.h` - Error codes and handling
- `ruby-custom-ext.h` - Custom extension callback API (for static extensions)
- `log-listener.h` - Logging callback interfaces

## Architecture Notes

### Static vs Dynamic Linking

This library supports both static and dynamic linking:

- **Static** (`BUILD_SHARED_LIBS=OFF`): Creates `libembedded-ruby.a` with all symbols
- **Dynamic** (`BUILD_SHARED_LIBS=ON`): Creates `libembedded-ruby.so`

For custom extensions, **static linking** is recommended as it ensures all extension symbols are available at runtime without dynamic loading complexity.

### Extension Resolution

When Ruby executes `require 'extension_name'`:

1. Check **statically-registered extensions** (via `Init_ext()` + custom callback)
2. If not found, attempt to load `extension_name.so` dynamically
3. If not found, raise LoadError

By using the custom callback, your extensions are found in step 1, making them immediately available without file I/O.

## Further Reading

- [Main Repository README](../../README.md) - Overall project documentation
- [CLAUDE.md](../../CLAUDE.md) - Technical deep-dive and architecture
- [Ruby Extension Writing Guide](https://docs.ruby-lang.org/en/master/extension_rdoc.html) - Official Ruby extension documentation
