#include <stdio.h>
#include <string.h>
#include "assets-vm-bridge.h"

RubyVMErrorCode assets_code_to_vm_code(AssetsErrorCode assets_code) {
    // Map asset-specific errors to VM error codes
    switch (assets_code) {
        case ASSETS_OK:
            return RUBY_VM_OK;

        case ASSETS_ERROR_INVALID_PARAM:
            return RUBY_VM_ERROR_INVALID_PARAM;

        // All asset-specific errors map to generic RUBY_VM_ERROR_ASSETS
        case ASSETS_ERROR_MKDIR:
        case ASSETS_ERROR_FILE_WRITE:
        case ASSETS_ERROR_FILE_READ:
        case ASSETS_ERROR_ZIP_EXTRACT:
        case ASSETS_ERROR_ZIP_OPEN:
        case ASSETS_ERROR_ZIP_ENTRY:
        case ASSETS_ERROR_MANIFEST_READ:
        case ASSETS_ERROR_MANIFEST_WRITE:
        case ASSETS_ERROR_VERIFICATION:
        case ASSETS_ERROR_CRC_MISMATCH:
        case ASSETS_ERROR_SIZE_MISMATCH:
        case ASSETS_ERROR_VERSION_MISMATCH:
        case ASSETS_ERROR_NO_EMBEDDED_DATA:
        case ASSETS_ERROR_MEMORY:
            return RUBY_VM_ERROR_ASSETS;

        default:
            return RUBY_VM_ERROR_ASSETS;
    }
}

RubyVMErrorCode assets_error_to_vm_error(const AssetsError* assets_error,
                                          AssetsErrorCode assets_code,
                                          RubyVMError* vm_error) {
    if (!vm_error) {
        return RUBY_VM_ERROR_INVALID_PARAM;
    }

    // Determine the assets error code to use
    AssetsErrorCode code = assets_error ? assets_error->code : assets_code;

    // Map to VM error code
    RubyVMErrorCode vm_code = assets_code_to_vm_code(code);
    vm_error->code = vm_code;

    // Build comprehensive error message with safe truncation
    // vm_error->message is 512 bytes
    // Format: "Assets error [CODE]: MESSAGE" or "Assets error [CODE]: MESSAGE (context: CTX)"
    // Fixed limits to guarantee no overflow: 40 (code) + 350 (msg) + 100 (ctx) + 30 (formatting) = 520
    // We use smaller limits to fit: 40 + 300 + 100 + 30 = 470 < 512 ✓

    if (assets_error && assets_error->message[0] != '\0') {
        const char* code_str = assets_error_string(code);

        if (assets_error->context[0] != '\0') {
            // Format with context - use precision specifiers to guarantee bounds
            snprintf(vm_error->message, sizeof(vm_error->message),
                    "Assets error [%.40s]: %.300s (context: %.100s)",
                    code_str,
                    assets_error->message,
                    assets_error->context);
        } else {
            // Format without context - use precision specifier to guarantee bounds
            snprintf(vm_error->message, sizeof(vm_error->message),
                    "Assets error [%.40s]: %.450s",
                    code_str,
                    assets_error->message);
        }
    } else {
        // No detailed error available, use generic message
        const char* code_str = assets_error_string(code);
        snprintf(vm_error->message, sizeof(vm_error->message),
                "Assets error: %.490s", code_str);
    }

    return vm_code;
}
