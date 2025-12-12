#ifndef ASSETS_ERROR_H
#define ASSETS_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Assets error codes
 */
typedef enum {
    ASSETS_OK = 0,
    ASSETS_ERROR_INVALID_PARAM = -1,
    ASSETS_ERROR_MKDIR = -2,
    ASSETS_ERROR_FILE_WRITE = -3,
    ASSETS_ERROR_FILE_READ = -4,
    ASSETS_ERROR_ZIP_EXTRACT = -5,
    ASSETS_ERROR_ZIP_OPEN = -6,
    ASSETS_ERROR_ZIP_ENTRY = -7,
    ASSETS_ERROR_MANIFEST_READ = -8,
    ASSETS_ERROR_MANIFEST_WRITE = -9,
    ASSETS_ERROR_VERIFICATION = -10,
    ASSETS_ERROR_CRC_MISMATCH = -11,
    ASSETS_ERROR_SIZE_MISMATCH = -12,
    ASSETS_ERROR_VERSION_MISMATCH = -13,
    ASSETS_ERROR_NO_EMBEDDED_DATA = -14,
    ASSETS_ERROR_MEMORY = -15,
} AssetsErrorCode;

/**
 * Error information structure
 */
typedef struct {
    AssetsErrorCode code;
    char message[512];
    char context[256];  // Additional context (file path, CRC values, etc.)
} AssetsError;

/**
 * Get human-readable error message for error code
 */
const char* assets_error_string(AssetsErrorCode code);

/**
 * Initialize error structure
 */
void assets_error_init(AssetsError* error);

/**
 * Set error with formatted message
 *
 * @param error Error structure to populate
 * @param code Error code
 * @param context Additional context (can be NULL)
 * @param fmt Format string for message
 */
void assets_error_set(AssetsError* error, AssetsErrorCode code,
                      const char* context, const char* fmt, ...);

/**
 * Set error with system errno context
 * Automatically includes strerror(errno) in the message
 *
 * @param error Error structure to populate
 * @param code Error code
 * @param context Additional context (e.g., file path)
 * @param fmt Format string for message (errno description will be appended)
 */
void assets_error_set_errno(AssetsError* error, AssetsErrorCode code,
                            const char* context, const char* fmt, ...);

#ifdef __cplusplus
}
#endif

#endif // ASSETS_ERROR_H
