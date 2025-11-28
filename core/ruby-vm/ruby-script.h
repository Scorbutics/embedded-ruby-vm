#ifndef RUBY_SCRIPT_H
#define RUBY_SCRIPT_H

#ifdef __cplusplus
extern "C" {
#endif

struct RubyScript {
    char* script_content;
};
typedef struct RubyScript RubyScript;

RubyScript* ruby_script_create_from_content(const char* content, size_t content_size);
void ruby_script_destroy(RubyScript* script);
const char* ruby_script_get_content(RubyScript* script);

#ifdef __cplusplus
}
#endif

#endif
