#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>

#include "logging.h"

// Configuration
#define LOG_BUFFER_SIZE 128
#define LOG_BUFFER_GROWTH_FACTOR 1.5
#define NUM_STREAMS 2
#define STDOUT_INDEX 0
#define STDERR_INDEX 1

// Log levels
enum {
    LOG_UNKNOWN = 0,
    LOG_DEFAULT,
    LOG_VERBOSE,
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_FATAL,
    LOG_SILENT
};

/**
 * Per-stream buffer state
 */
typedef struct {
    char* buffer;
    size_t size;
    size_t capacity;
    log_stream_t stream;
    int fd;           // File descriptor to read from
    int is_open;      // Whether this stream is still active
} stream_buffer_t;

// Thread control
static volatile int g_logging_thread_continue = 1;
static pthread_t g_logging_thread = 0;

// Stream pipes: [0] = stdout, [1] = stderr
static int stream_pfd[NUM_STREAMS][2] = {{-1, -1}, {-1, -1}};

// Logging configuration
static char* log_tag = NULL;
static logging_native_logging_func_t native_logging_func = NULL;
static logging_custom_output_func_t custom_output_func = NULL;
static void* custom_output_context = NULL;

/**
 * Write log message to native logging system
 */
static void call_native_logging_function(int prio, const char* tag, const char* text) {
    if (native_logging_func != NULL) {
        native_logging_func(prio, tag, text);
    }
}

/**
 * Output a complete log line to all configured outputs
 */
static void write_full_log_line(const char* line, log_stream_t stream) {
    const char* tag = (log_tag != NULL) ? log_tag : "UNKNOWN";
    int priority = (stream == LOG_STREAM_STDERR) ? LOG_ERROR : LOG_INFO;

    call_native_logging_function(priority, tag, line);

    if (custom_output_func != NULL) {
        custom_output_func(line, stream, custom_output_context);
    }
}

/**
 * Flush buffer as a complete line
 */
static void send_stream_buffer_to_output_as_line(stream_buffer_t* sb) {
    if (sb->size > 0) {
        sb->buffer[sb->size] = '\0';
        write_full_log_line(sb->buffer, sb->stream);
        sb->size = 0;
    }
}

/**
 * Resize buffer if needed to accommodate new data
 */
static int resize_stream_buffer_if_needed(stream_buffer_t* sb, size_t newSize) {
    if (sb->capacity <= newSize) {
        size_t newCapacity = (size_t)(newSize * LOG_BUFFER_GROWTH_FACTOR) + 1;
        char* newBuffer = (char*)realloc(sb->buffer, newCapacity);

        if (newBuffer == NULL) {
            return 1;
        }

        sb->buffer = newBuffer;
        sb->capacity = newCapacity;
    }
    return 0;
}

/**
 * Append data to the stream buffer
 */
static int append_to_stream_buffer(stream_buffer_t* sb, const char* data, size_t dataSize) {
    if (resize_stream_buffer_if_needed(sb, sb->size + dataSize + 1) != 0) {
        call_native_logging_function(LOG_ERROR, log_tag, "Memory allocation failed");
        return 1;
    }

    memcpy(sb->buffer + sb->size, data, dataSize);
    sb->size += dataSize;
    return 0;
}

/**
 * Initialize a stream buffer
 */
static int init_stream_buffer(stream_buffer_t* sb, log_stream_t stream, int fd) {
    sb->buffer = (char*)malloc(LOG_BUFFER_SIZE);
    if (sb->buffer == NULL) {
        return -1;
    }
    sb->size = 0;
    sb->capacity = LOG_BUFFER_SIZE;
    sb->stream = stream;
    sb->fd = fd;
    sb->is_open = 1;
    return 0;
}

/**
 * Free a stream buffer
 */
static void free_stream_buffer(stream_buffer_t* sb) {
    if (sb->buffer != NULL) {
        free(sb->buffer);
        sb->buffer = NULL;
    }
    sb->size = 0;
    sb->capacity = 0;
}

/**
 * Process data from a file descriptor
 * Returns number of bytes processed, -1 on error, 0 on EOF
 */
static ssize_t process_stream_data(stream_buffer_t* sb) {
    char buf[LOG_BUFFER_SIZE];
    ssize_t readSize = read(sb->fd, buf, sizeof(buf));

    if (readSize <= 0) {
        return readSize;
    }

    size_t lineStart = 0;

    for (ssize_t i = 0; i < readSize; i++) {
        if (buf[i] == '\n') {
            if (append_to_stream_buffer(sb, buf + lineStart, i - lineStart) != 0) {
                return -1;
            }
            send_stream_buffer_to_output_as_line(sb);
            lineStart = i + 1;
        }
    }

    // Append remaining incomplete line
    if (readSize > (ssize_t)lineStart) {
        if (append_to_stream_buffer(sb, buf + lineStart, readSize - lineStart) != 0) {
            return -1;
        }
    }

    return readSize;
}

/**
 * Background thread that reads from redirected stdout/stderr using select()
 */
static void* logging_function_thread(void* unused) {
    (void)unused;

    stream_buffer_t streams[NUM_STREAMS];

    // Initialize all stream buffers
    if (init_stream_buffer(&streams[STDOUT_INDEX], LOG_STREAM_STDOUT, stream_pfd[STDOUT_INDEX][0]) != 0 ||
        init_stream_buffer(&streams[STDERR_INDEX], LOG_STREAM_STDERR, stream_pfd[STDERR_INDEX][0]) != 0) {
        call_native_logging_function(LOG_ERROR, log_tag, "Failed to allocate buffers, aborting logging thread");
        return NULL;
    }

    // Find max fd for select()
    int max_fd = streams[STDOUT_INDEX].fd;
    for (int i = 1; i < NUM_STREAMS; i++) {
        if (streams[i].fd > max_fd) {
            max_fd = streams[i].fd;
        }
    }

    // Main read loop using select()
    while (g_logging_thread_continue) {
        // Check if any stream is still open
        int any_open = 0;
        for (int i = 0; i < NUM_STREAMS; i++) {
            if (streams[i].is_open) {
                any_open = 1;
                break;
            }
        }
        if (!any_open) break;

        fd_set readfds;
        FD_ZERO(&readfds);

        // Add all open streams to fd_set
        for (int i = 0; i < NUM_STREAMS; i++) {
            if (streams[i].is_open) {
                FD_SET(streams[i].fd, &readfds);
            }
        }

        // Wait for data on any descriptor
        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int select_result = select(max_fd + 1, &readfds, NULL, NULL, &timeout);

        if (select_result < 0) {
            if (errno == EINTR) {
                continue;
            }
            char errorMessage[256];
            snprintf(errorMessage, sizeof(errorMessage),
                     "select() error: %s", strerror(errno));
            call_native_logging_function(LOG_ERROR, log_tag, errorMessage);
            break;
        }

        if (select_result == 0) {
            continue; // Timeout
        }

        // Process all ready streams
        for (int i = 0; i < NUM_STREAMS; i++) {
            if (streams[i].is_open && FD_ISSET(streams[i].fd, &readfds)) {
                ssize_t result = process_stream_data(&streams[i]);
                if (result == 0) {
                    // EOF
                    send_stream_buffer_to_output_as_line(&streams[i]);
                    streams[i].is_open = 0;
                } else if (result < 0) {
                    const char* stream_name = (i == STDOUT_INDEX) ? "stdout" : "stderr";
                    char errorMessage[256];
                    snprintf(errorMessage, sizeof(errorMessage),
                             "Error reading %s: %s", stream_name, strerror(errno));
                    call_native_logging_function(LOG_ERROR, log_tag, errorMessage);
                    streams[i].is_open = 0;
                }
            }
        }
    }

    // Flush all remaining buffered data
    for (int i = 0; i < NUM_STREAMS; i++) {
        send_stream_buffer_to_output_as_line(&streams[i]);
        free_stream_buffer(&streams[i]);
    }

    write_full_log_line("----------------------------", LOG_STREAM_STDOUT);
    call_native_logging_function(LOG_DEBUG, log_tag, "Logging thread ended");

    return NULL;
}


/**
 * Create a socketpair and redirect a file descriptor
 */
static int create_and_redirect_stream(int stream_index, int target_fd, const char* stream_name) {
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, stream_pfd[stream_index]) == -1) {
        char error[256];
        snprintf(error, sizeof(error), "socketpair() failed for %s", stream_name);
        call_native_logging_function(LOG_ERROR, "Logging", error);
        return -1;
    }

    if (dup2(stream_pfd[stream_index][1], target_fd) == -1) {
        char error[256];
        snprintf(error, sizeof(error), "dup2() failed for %s", stream_name);
        call_native_logging_function(LOG_ERROR, "Logging", error);
        return -1;
    }

    close(stream_pfd[stream_index][1]);
    stream_pfd[stream_index][1] = -1;

    return 0;
}

/**
 * Cleanup all stream resources
 */
static void cleanup_streams(void) {
    for (int i = 0; i < NUM_STREAMS; i++) {
        for (int j = 0; j < 2; j++) {
            if (stream_pfd[i][j] != -1) {
                close(stream_pfd[i][j]);
                stream_pfd[i][j] = -1;
            }
        }
    }
}

/**
 * Set native logging function
 */
void logging_set_native_function(logging_native_logging_func_t func) {
    native_logging_func = func;
}

/**
 * Set custom output callback
 */
void logging_set_custom_output_callback(logging_custom_output_func_t func, void* context) {
    custom_output_func = func;
    custom_output_context = context;
}


/**
 * Start the logging thread and redirect stdout/stderr
 */
int logging_thread_run(const char* appname) {
    if (appname == NULL) {
        return -1;
    }

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    log_tag = strdup(appname);
    if (log_tag == NULL) {
        call_native_logging_function(LOG_ERROR, "Logging", "Failed to allocate tag");
        return -2;
    }

    // Create and redirect both streams
    if (create_and_redirect_stream(STDOUT_INDEX, STDOUT_FILENO, "stdout") != 0) {
        free(log_tag);
        log_tag = NULL;
        cleanup_streams();
        return -3;
    }

    if (create_and_redirect_stream(STDERR_INDEX, STDERR_FILENO, "stderr") != 0) {
        free(log_tag);
        log_tag = NULL;
        cleanup_streams();
        return -4;
    }

    // Start logging thread
    if (pthread_create(&g_logging_thread, NULfunction_L, logging_function_thread, NULL) != 0) {
        call_native_logging_function(LOG_WARN, log_tag, "Failed to create logging thread");
        cleanup_streams();
        free(log_tag);
        log_tag = NULL;
        return -5;
    }

    call_native_logging_function(LOG_DEBUG, log_tag, "Logging thread started");
    return 0;
}

/**
 * Stop the logging thread gracefully
 */
int logging_thread_stop(void) {
    if (g_logging_thread != 0) {
        g_logging_thread_continue = 0;

        // Close read ends to unblock the thread
        for (int i = 0; i < NUM_STREAMS; i++) {
            if (stream_pfd[i][0] != -1) {
                close(stream_pfd[i][0]);
                stream_pfd[i][0] = -1;
            }
        }

        int result = pthread_join(g_logging_thread, NULL);
        if (result != 0) {
            call_native_logging_function(LOG_WARN, log_tag, "Failed to join logging thread");
            return -1;
        }

        g_logging_thread = 0;

        if (log_tag != NULL) {
            free(log_tag);
            log_tag = NULL;
        }
    }
    return 0;
}
