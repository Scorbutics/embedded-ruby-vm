#ifndef LOG_LISTENER_H
#define LOG_LISTENER_H

#include "embedded-ruby-vm/logging.h"

#ifdef __cplusplus
extern "C" {
#endif

struct LogListener;

typedef void (*LogAcceptFunc)(struct LogListener* listener, const char* lineMessage);
typedef void (*LogErrorFunc)(struct LogListener* listener, const char* errorMessage);
typedef void (*LogMessageFunc)(struct LogListener* listener, const char* message, log_stream_t source);

typedef struct LogListener {
    void* context;
    void* user_data;
    LogAcceptFunc accept;           // Legacy callback for stdout (deprecated)
    LogErrorFunc on_log_error;      // Legacy callback for stderr (deprecated)
    LogMessageFunc on_log_message;  // New callback with source information
} LogListener;

#ifdef __cplusplus
}
#endif

#endif
