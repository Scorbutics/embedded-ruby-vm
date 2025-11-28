#ifndef RUBY_VM_ERROR_H
#define RUBY_VM_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Ruby VM error codes
 */
typedef enum {
    RUBY_VM_OK = 0,
    RUBY_VM_ERROR_INVALID_PARAM = -1,
    RUBY_VM_ERROR_COMM_CHANNEL = -2,
    RUBY_VM_ERROR_THREAD_CREATE = -3,
    RUBY_VM_ERROR_LOGGING = -4,
    RUBY_VM_ERROR_RUBY_INIT = -5,
    RUBY_VM_ERROR_RUBY_EXEC = -6,
    RUBY_VM_ERROR_TIMEOUT = -7,
    RUBY_VM_ERROR_ALREADY_STARTED = -8,
} RubyVMErrorCode;

/**
 * Get human-readable error message for error code
 */
const char* ruby_vm_error_string(RubyVMErrorCode code);

/**
 * Error information structure
 */
typedef struct {
    RubyVMErrorCode code;
    char message[512];  // Detailed error message
} RubyVMError;

/**
 * Initialize error structure
 */
void ruby_vm_error_init(RubyVMError* error);

/**
 * Set error with formatted message
 */
void ruby_vm_error_set(RubyVMError* error, RubyVMErrorCode code, const char* fmt, ...);

#ifdef __cplusplus
}
#endif

#endif // RUBY_VM_ERROR_H
