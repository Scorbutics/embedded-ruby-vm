#include "ruby-api-loader.h"
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#define LOAD_SYM(handle, api_ptr, name) \
    do { \
        *(void**)&(api_ptr) = dlsym(handle, name); \
        if (!(api_ptr)) { \
            fprintf(stderr, "Failed to load symbol '%s': %s\n", name, dlerror()); \
            dlclose(handle); \
            return -1; \
        } \
    } while(0)

int ruby_api_load(const char* lib_path, RubyAPI* api) {
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

void ruby_api_unload(RubyAPI* api) {
    if (api && api->handle) {
        dlclose(api->handle);
        memset(api, 0, sizeof(RubyAPI));
    }
}
