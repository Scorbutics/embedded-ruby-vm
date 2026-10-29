#ifndef RUBY_SCRIPT_H
#define RUBY_SCRIPT_H

#include "embedded-ruby-vm/ruby-api.h"

#ifdef __cplusplus
extern "C" {
#endif

struct RubyScript {
    char* script_content;
};

const char* ruby_script_get_content(RubyScript* script);

#ifdef __cplusplus
}
#endif

#endif
