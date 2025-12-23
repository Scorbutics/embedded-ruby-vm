#ifndef LOG_LISTENER_H
#define LOG_LISTENER_H

#ifdef __cplusplus
extern "C" {
#endif

struct LogListener;

typedef void (*LogAcceptFunc)(struct LogListener* listener, const char* lineMessage);
typedef void (*LogErrorFunc)(struct LogListener* listener, const char* errorMessage);

typedef struct LogListener {
    void* context;
    void* user_data;
    LogAcceptFunc accept;
    LogErrorFunc on_log_error;
} LogListener;

#ifdef __cplusplus
}
#endif

#endif
