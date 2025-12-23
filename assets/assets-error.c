#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include "embedded-ruby-vm/assets-error.h"

const char* assets_error_string(AssetsErrorCode code) {
    switch (code) {
        case ASSETS_OK:
            return "Success";
        case ASSETS_ERROR_INVALID_PARAM:
            return "Invalid parameter";
        case ASSETS_ERROR_MKDIR:
            return "Failed to create directory";
        case ASSETS_ERROR_FILE_WRITE:
            return "Failed to write file";
        case ASSETS_ERROR_FILE_READ:
            return "Failed to read file";
        case ASSETS_ERROR_ZIP_EXTRACT:
            return "Failed to extract zip file";
        case ASSETS_ERROR_ZIP_OPEN:
            return "Failed to open zip file";
        case ASSETS_ERROR_ZIP_ENTRY:
            return "Failed to process zip entry";
        case ASSETS_ERROR_MANIFEST_READ:
            return "Failed to read manifest file";
        case ASSETS_ERROR_MANIFEST_WRITE:
            return "Failed to write manifest file";
        case ASSETS_ERROR_VERIFICATION:
            return "Installation verification failed";
        case ASSETS_ERROR_CRC_MISMATCH:
            return "CRC checksum mismatch";
        case ASSETS_ERROR_SIZE_MISMATCH:
            return "File size mismatch";
        case ASSETS_ERROR_VERSION_MISMATCH:
            return "Version mismatch";
        case ASSETS_ERROR_NO_EMBEDDED_DATA:
            return "No embedded data available";
        case ASSETS_ERROR_MEMORY:
            return "Memory allocation failed";
        default:
            return "Unknown error";
    }
}

void assets_error_init(AssetsError* error) {
    if (!error) return;
    error->code = ASSETS_OK;
    error->message[0] = '\0';
    error->context[0] = '\0';
}

void assets_error_set(AssetsError* error, AssetsErrorCode code,
                      const char* context, const char* fmt, ...) {
    if (!error) return;

    error->code = code;

    // Set context
    if (context) {
        snprintf(error->context, sizeof(error->context), "%s", context);
    } else {
        error->context[0] = '\0';
    }

    // Set message
    if (fmt) {
        va_list args;
        va_start(args, fmt);
        vsnprintf(error->message, sizeof(error->message), fmt, args);
        va_end(args);
    } else {
        snprintf(error->message, sizeof(error->message), "%s", assets_error_string(code));
    }
}

void assets_error_set_errno(AssetsError* error, AssetsErrorCode code,
                            const char* context, const char* fmt, ...) {
    if (!error) return;

    error->code = code;

    // Set context
    if (context) {
        snprintf(error->context, sizeof(error->context), "%s", context);
    } else {
        error->context[0] = '\0';
    }

    // Build message with errno
    char user_message[256];
    if (fmt) {
        va_list args;
        va_start(args, fmt);
        vsnprintf(user_message, sizeof(user_message), fmt, args);
        va_end(args);
    } else {
        snprintf(user_message, sizeof(user_message), "%s", assets_error_string(code));
    }

    // Append errno description
    snprintf(error->message, sizeof(error->message), "%s: %s",
             user_message, strerror(errno));
}
