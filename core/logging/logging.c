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

/**
 * Linked list node for native logging functions
 */
typedef struct native_logger_node {
    logging_native_logging_func_t func;
    struct native_logger_node* next;
} native_logger_node_t;

/**
 * Linked list node for custom output callbacks
 */
typedef struct custom_output_node {
    logging_custom_output_func_t func;
    void* context;
    struct custom_output_node* next;
} custom_output_node_t;

/**
 * Main logging state structure
 */
typedef struct {
    char* log_tag;
    pthread_t logging_thread;
    volatile int thread_continue;
    int stream_pfd[NUM_STREAMS][2];

    native_logger_node_t* native_loggers;
    custom_output_node_t* custom_outputs;
    int custom_output_count;

    pthread_mutex_t lock;
    int is_initialized;
    int is_running;
} logging_state_t;

// Global logging state
static logging_state_t g_logging_state = {
    .log_tag = NULL,
    .logging_thread = 0,
    .thread_continue = 0,
    .stream_pfd = {{-1, -1}, {-1, -1}},
    .native_loggers = NULL,
    .custom_outputs = NULL,
    .custom_output_count = 0,
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .is_initialized = 0,
    .is_running = 0
};

/**
 * Write log message to all native logging systems
 */
static void call_native_logging_function(int prio, const char* tag, const char* text) {
    pthread_mutex_lock(&g_logging_state.lock);

    native_logger_node_t* current = g_logging_state.native_loggers;
    while (current != NULL) {
        if (current->func != NULL) {
            current->func(prio, tag, text);
        }
        current = current->next;
    }

    pthread_mutex_unlock(&g_logging_state.lock);
}

/**
 * Write log message to all custom output callbacks
 */
static void call_custom_logging_function(log_stream_t stream, const char* line) {
    pthread_mutex_lock(&g_logging_state.lock);

    custom_output_node_t* current = g_logging_state.custom_outputs;
    while (current != NULL) {
        if (current->func != NULL) {
            current->func(line, stream, current->context);
        }
        current = current->next;
    }

    pthread_mutex_unlock(&g_logging_state.lock);
}

/**
 * Output a complete log line to all configured outputs
 */
static void write_full_log_line(const char* line, log_stream_t stream) {
    const char* tag = (g_logging_state.log_tag != NULL) ? g_logging_state.log_tag : "UNKNOWN";
    int priority = (stream == LOG_STREAM_STDERR) ? LOG_ERROR : LOG_INFO;

    call_native_logging_function(priority, tag, line);

    call_custom_logging_function(stream, line);
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
        call_native_logging_function(LOG_ERROR, g_logging_state.log_tag, "Memory allocation failed");
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
    if (init_stream_buffer(&streams[STDOUT_INDEX], LOG_STREAM_STDOUT, g_logging_state.stream_pfd[STDOUT_INDEX][0]) != 0 ||
        init_stream_buffer(&streams[STDERR_INDEX], LOG_STREAM_STDERR, g_logging_state.stream_pfd[STDERR_INDEX][0]) != 0) {
        call_native_logging_function(LOG_ERROR, g_logging_state.log_tag, "Failed to allocate buffers, aborting logging thread");
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
    while (g_logging_state.thread_continue) {
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
            call_native_logging_function(LOG_ERROR, g_logging_state.log_tag, errorMessage);
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
                    call_native_logging_function(LOG_ERROR, g_logging_state.log_tag, errorMessage);
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
    call_native_logging_function(LOG_DEBUG, g_logging_state.log_tag, "Logging thread ended");

    return NULL;
}


/**
 * Create a socketpair and redirect a file descriptor
 */
static int create_and_redirect_stream(int stream_index, int target_fd, const char* stream_name) {
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, g_logging_state.stream_pfd[stream_index]) == -1) {
        char error[256];
        snprintf(error, sizeof(error), "socketpair() failed for %s", stream_name);
        call_native_logging_function(LOG_ERROR, "Logging", error);
        return -1;
    }

    if (dup2(g_logging_state.stream_pfd[stream_index][1], target_fd) == -1) {
        char error[256];
        snprintf(error, sizeof(error), "dup2() failed for %s", stream_name);
        call_native_logging_function(LOG_ERROR, "Logging", error);
        return -1;
    }

    close(g_logging_state.stream_pfd[stream_index][1]);
    g_logging_state.stream_pfd[stream_index][1] = -1;

    return 0;
}

/**
 * Cleanup all stream resources
 */
static void cleanup_streams(void) {
    for (int i = 0; i < NUM_STREAMS; i++) {
        for (int j = 0; j < 2; j++) {
            if (g_logging_state.stream_pfd[i][j] != -1) {
                close(g_logging_state.stream_pfd[i][j]);
                g_logging_state.stream_pfd[i][j] = -1;
            }
        }
    }
}

/**
 * Internal: Start the logging thread and redirect stdout/stderr
 * Must be called with lock held
 */
static int internal_start_logging_thread(void) {
    if (g_logging_state.is_running) {
        return 0; // Already running
    }

    if (!g_logging_state.is_initialized) {
        call_native_logging_function(LOG_ERROR, "Logging", "Logging not initialized");
        return -1;
    }

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    // Create and redirect both streams
    if (create_and_redirect_stream(STDOUT_INDEX, STDOUT_FILENO, "stdout") != 0) {
        cleanup_streams();
        return -2;
    }

    if (create_and_redirect_stream(STDERR_INDEX, STDERR_FILENO, "stderr") != 0) {
        cleanup_streams();
        return -3;
    }

    // Start logging thread
    g_logging_state.thread_continue = 1;
    if (pthread_create(&g_logging_state.logging_thread, NULL, logging_function_thread, NULL) != 0) {
        call_native_logging_function(LOG_WARN, g_logging_state.log_tag, "Failed to create logging thread");
        cleanup_streams();
        return -4;
    }

    g_logging_state.is_running = 1;
    call_native_logging_function(LOG_DEBUG, g_logging_state.log_tag, "Logging thread started");
    return 0;
}

/**
 * Internal: Stop the logging thread gracefully
 * Must be called with lock held
 */
static int internal_stop_logging_thread(void) {
    if (!g_logging_state.is_running) {
        return 0; // Already stopped
    }

    g_logging_state.thread_continue = 0;

    // Close read ends to unblock the thread
    for (int i = 0; i < NUM_STREAMS; i++) {
        if (g_logging_state.stream_pfd[i][0] != -1) {
            close(g_logging_state.stream_pfd[i][0]);
            g_logging_state.stream_pfd[i][0] = -1;
        }
    }

    // Unlock before joining to allow thread to complete
    pthread_mutex_unlock(&g_logging_state.lock);
    int result = pthread_join(g_logging_state.logging_thread, NULL);
    pthread_mutex_lock(&g_logging_state.lock);

    if (result != 0) {
        call_native_logging_function(LOG_WARN, g_logging_state.log_tag, "Failed to join logging thread");
        return -1;
    }

    g_logging_state.logging_thread = 0;
    g_logging_state.is_running = 0;

    return 0;
}

/**
 * Initialize the logging system
 */
int logging_init(const char* appname) {
    if (appname == NULL) {
        return -1;
    }

    pthread_mutex_lock(&g_logging_state.lock);

    if (g_logging_state.is_initialized) {
        pthread_mutex_unlock(&g_logging_state.lock);
        return 0; // Already initialized
    }

    g_logging_state.log_tag = strdup(appname);
    if (g_logging_state.log_tag == NULL) {
        pthread_mutex_unlock(&g_logging_state.lock);
        call_native_logging_function(LOG_ERROR, appname, "Failed to allocate tag");
        return -2;
    }

    g_logging_state.is_initialized = 1;

    pthread_mutex_unlock(&g_logging_state.lock);
    return 0;
}

/**
 * Shutdown the logging system
 */
int logging_shutdown(void) {
    pthread_mutex_lock(&g_logging_state.lock);

    if (!g_logging_state.is_initialized) {
        pthread_mutex_unlock(&g_logging_state.lock);
        return 0;
    }

    // Stop thread if running
    if (g_logging_state.is_running) {
        internal_stop_logging_thread();
    }

    // Free all native loggers
    native_logger_node_t* native_current = g_logging_state.native_loggers;
    while (native_current != NULL) {
        native_logger_node_t* next = native_current->next;
        free(native_current);
        native_current = next;
    }
    g_logging_state.native_loggers = NULL;

    // Free all custom outputs
    custom_output_node_t* custom_current = g_logging_state.custom_outputs;
    while (custom_current != NULL) {
        custom_output_node_t* next = custom_current->next;
        free(custom_current);
        custom_current = next;
    }
    g_logging_state.custom_outputs = NULL;
    g_logging_state.custom_output_count = 0;

    // Free log tag
    if (g_logging_state.log_tag != NULL) {
        free(g_logging_state.log_tag);
        g_logging_state.log_tag = NULL;
    }

    g_logging_state.is_initialized = 0;

    pthread_mutex_unlock(&g_logging_state.lock);
    return 0;
}

/**
 * Add a native logging function
 */
int logging_add_native_function(logging_native_logging_func_t func) {
    if (func == NULL) {
        return -1;
    }

    pthread_mutex_lock(&g_logging_state.lock);

    // Check if already exists
    native_logger_node_t* current = g_logging_state.native_loggers;
    while (current != NULL) {
        if (current->func == func) {
            pthread_mutex_unlock(&g_logging_state.lock);
            return 0; // Already added
        }
        current = current->next;
    }

    // Create new node
    native_logger_node_t* new_node = (native_logger_node_t*)malloc(sizeof(native_logger_node_t));
    if (new_node == NULL) {
        pthread_mutex_unlock(&g_logging_state.lock);
        return -2;
    }

    new_node->func = func;
    new_node->next = g_logging_state.native_loggers;
    g_logging_state.native_loggers = new_node;

    pthread_mutex_unlock(&g_logging_state.lock);
    return 0;
}

/**
 * Remove a native logging function
 */
int logging_remove_native_function(logging_native_logging_func_t func) {
    if (func == NULL) {
        return -1;
    }

    pthread_mutex_lock(&g_logging_state.lock);

    native_logger_node_t* current = g_logging_state.native_loggers;
    native_logger_node_t* prev = NULL;

    while (current != NULL) {
        if (current->func == func) {
            if (prev == NULL) {
                g_logging_state.native_loggers = current->next;
            } else {
                prev->next = current->next;
            }
            free(current);
            pthread_mutex_unlock(&g_logging_state.lock);
            return 0;
        }
        prev = current;
        current = current->next;
    }

    pthread_mutex_unlock(&g_logging_state.lock);
    return -1; // Not found
}

/**
 * Add a custom output callback
 */
int logging_add_custom_output(logging_custom_output_func_t func, void* context) {
    if (func == NULL) {
        return -1;
    }
    
    if (g_logging_state.is_initialized == 0) {
        call_native_logging_function(LOG_ERROR, "Logging", "Logging not initialized");
        return -2;
    }

    pthread_mutex_lock(&g_logging_state.lock);

    // Check if already exists (same func and context)
    custom_output_node_t* current = g_logging_state.custom_outputs;
    while (current != NULL) {
        if (current->func == func && current->context == context) {
            pthread_mutex_unlock(&g_logging_state.lock);
            return 0; // Already added
        }
        current = current->next;
    }

    // Create new node
    custom_output_node_t* new_node = (custom_output_node_t*)malloc(sizeof(custom_output_node_t));
    if (new_node == NULL) {
        pthread_mutex_unlock(&g_logging_state.lock);
        return -3;
    }

    new_node->func = func;
    new_node->context = context;
    new_node->next = g_logging_state.custom_outputs;
    g_logging_state.custom_outputs = new_node;
    g_logging_state.custom_output_count++;

    // Start logging thread if this is the first custom output
    if (g_logging_state.custom_output_count == 1) {
        int result = internal_start_logging_thread();
        if (result != 0) {
            // Remove the node we just added
            g_logging_state.custom_outputs = new_node->next;
            g_logging_state.custom_output_count--;
            free(new_node);
            pthread_mutex_unlock(&g_logging_state.lock);
            return result;
        }
    }

    pthread_mutex_unlock(&g_logging_state.lock);
    return 0;
}

/**
 * Remove a custom output callback
 */
int logging_remove_custom_output(logging_custom_output_func_t func, void* context) {
    if (func == NULL) {
        return -1;
    }

    pthread_mutex_lock(&g_logging_state.lock);

    custom_output_node_t* current = g_logging_state.custom_outputs;
    custom_output_node_t* prev = NULL;

    while (current != NULL) {
        if (current->func == func && current->context == context) {
            if (prev == NULL) {
                g_logging_state.custom_outputs = current->next;
            } else {
                prev->next = current->next;
            }
            free(current);
            g_logging_state.custom_output_count--;

            // Stop logging thread if this was the last custom output
            if (g_logging_state.custom_output_count == 0) {
                internal_stop_logging_thread();
            }

            pthread_mutex_unlock(&g_logging_state.lock);
            return 0;
        }
        prev = current;
        current = current->next;
    }

    pthread_mutex_unlock(&g_logging_state.lock);
    return -1; // Not found
}
