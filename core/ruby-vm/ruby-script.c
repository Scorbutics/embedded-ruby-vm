#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"
#include "ruby-script.h"

RubyScript* ruby_script_create_from_content(const char* content, const size_t content_size) {
    if (!content) return NULL;

    RubyScript* script = malloc(sizeof(RubyScript));
    if (!script) return NULL;

    script->script_content = strndup(content, content_size);
    if (!script->script_content) {
        free(script);
        return NULL;
    }

    return script;
}

void ruby_script_destroy(RubyScript* script) {
    if (!script) return;

    free(script->script_content);
    free(script);
}

const char* ruby_script_get_content(RubyScript* script) {
    return script ? script->script_content : NULL;
}
