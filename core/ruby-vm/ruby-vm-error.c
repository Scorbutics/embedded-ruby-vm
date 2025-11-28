#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "ruby-vm-error.h"

const char* ruby_vm_error_string(RubyVMErrorCode code) {
    switch (code) {
        case RUBY_VM_OK:
            return "Success";
        case RUBY_VM_ERROR_INVALID_PARAM:
            return "Invalid parameter";
        case RUBY_VM_ERROR_COMM_CHANNEL:
            return "Failed to create communication channel";
        case RUBY_VM_ERROR_THREAD_CREATE:
            return "Failed to create thread";
        case RUBY_VM_ERROR_LOGGING:
            return "Logging system error";
        case RUBY_VM_ERROR_RUBY_INIT:
            return "Ruby initialization failed";
        case RUBY_VM_ERROR_RUBY_EXEC:
            return "Ruby execution failed";
        case RUBY_VM_ERROR_TIMEOUT:
            return "Operation timed out";
        case RUBY_VM_ERROR_ALREADY_STARTED:
            return "VM already started";
        default:
            return "Unknown error";
    }
}

void ruby_vm_error_init(RubyVMError* error) {
    if (!error) return;
    error->code = RUBY_VM_OK;
    error->message[0] = '\0';
}

void ruby_vm_error_set(RubyVMError* error, RubyVMErrorCode code, const char* fmt, ...) {
    if (!error) return;

    error->code = code;

    if (fmt) {
        va_list args;
        va_start(args, fmt);
        vsnprintf(error->message, sizeof(error->message), fmt, args);
        va_end(args);
    } else {
        snprintf(error->message, sizeof(error->message), "%s", ruby_vm_error_string(code));
    }
}
