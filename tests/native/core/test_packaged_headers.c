/*
 * The released include directory is enough to use the library:
 *   - this file is compiled with only the packaged include directory
 *     (build/include, what CI ships) on its include path: no private header;
 *   - ruby-api-loader.h is the generated one, and gives a usable API.
 */

#include <stdio.h>

#include "embedded-ruby-vm/assets-error.h"
#include "embedded-ruby-vm/assets-install.h"
#include "embedded-ruby-vm/completion-task.h"
#include "embedded-ruby-vm/log-listener.h"
#include "embedded-ruby-vm/remote-options.h"
#include "embedded-ruby-vm/ruby-api-loader.h"
#include "embedded-ruby-vm/ruby-api.h"
#include "embedded-ruby-vm/ruby-custom-ext.h"

int main(void) {
    RubyAPI api;
    const char* deps_paths[] = {
        "../lib/libembedded-ruby.deps",
        "./libembedded-ruby.deps",
        "libembedded-ruby.deps",
        NULL
    };
    const char* lib_paths[] = {
        "../lib/libembedded-ruby.so",
        "./libembedded-ruby.so",
        "libembedded-ruby.so",
        NULL
    };

    if (ruby_api_bootstrap(&api, deps_paths, lib_paths, NULL) != 0) {
        fprintf(stderr, "FAIL: ruby_api_bootstrap\n");
        return 1;
    }
    if (api.interpreter.create == NULL || api.interpreter.execute_sync == NULL
        || api.script.create_from_content == NULL || api.set_custom_ext_init == NULL) {
        fprintf(stderr, "FAIL: the loaded API misses functions\n");
        ruby_api_unload(&api);
        return 1;
    }
    ruby_api_unload(&api);

    fprintf(stderr, "PASS: the packaged headers build a consumer of the library\n");
    return 0;
}
