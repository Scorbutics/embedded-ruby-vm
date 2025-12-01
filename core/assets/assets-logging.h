#ifndef ASSETS_LOGGING_H
#define ASSETS_LOGGING_H

#include <stdio.h>

/**
 * Assets logging system
 *
 * Provides three levels of logging:
 * - DEBUG: Verbose debug information (disabled in release builds)
 * - INFO: Informational messages (always enabled, goes to stdout)
 * - ERROR: Error messages (always enabled, goes to stderr)
 */

// Debug logging - can be disabled by defining NDEBUG
#ifdef NDEBUG
    #define ASSETS_DEBUG_LOG(fmt, ...) ((void)0)
#else
    #define ASSETS_DEBUG_LOG(fmt, ...) do { \
        fprintf(stderr, "[ASSETS_DEBUG] " fmt "\n", ##__VA_ARGS__); \
        fflush(stderr); \
    } while(0)
#endif

// Info logging - always enabled, goes to stdout
#define ASSETS_INFO_LOG(fmt, ...) do { \
    fprintf(stdout, "[ASSETS] " fmt "\n", ##__VA_ARGS__); \
    fflush(stdout); \
} while(0)

// Error logging - always enabled, goes to stderr
#define ASSETS_ERROR_LOG(fmt, ...) do { \
    fprintf(stderr, "[ASSETS_ERROR] " fmt "\n", ##__VA_ARGS__); \
    fflush(stderr); \
} while(0)

#endif // ASSETS_LOGGING_H
