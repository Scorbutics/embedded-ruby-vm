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
#include <signal.h>
#include <time.h>

#include "embedded-ruby-vm/logging.h"
#include "embedded-ruby-vm/jni_logging.h"
#include "embedded-ruby-vm/constants.h"

// Configuration
#define LOG_BUFFER_SIZE 128
#define LOG_BUFFER_GROWTH_FACTOR 1.5
#define NUM_STREAMS 5
#define RUBY_STDOUT_INDEX 0
#define RUBY_STDERR_INDEX 1
#define VMLOGGER_INDEX 2
#define NATIVE_STDOUT_INDEX 3
#define NATIVE_STDERR_INDEX 4

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
 * Linked list node for per-interpreter listener registry. See the kdoc in
 * logging.h above [logging_register_interpreter_listener] for the design
 * rationale (replaces the old single-slot vm->log_listener swap dance).
 * Append-order matters: the head of the list IS the oldest registration
 * and is the fallback for native (untagged) log lines.
 */
typedef struct interpreter_listener_node {
    int interpreter_id;
    LogListener listener;
    struct interpreter_listener_node* next;
} interpreter_listener_node_t;

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
    /* Head = oldest registered interpreter (the natural fallback sink
     * for untagged native lines). Mutations and dispatch reads are
     * serialized through the shared `lock` (same mutex that protects
     * custom_outputs). */
    interpreter_listener_node_t* interpreter_listeners;
    int interpreter_listener_count;

    int original_stdout_fd;  // Terminal FD saved before dup2 redirect
    int original_stderr_fd;  // Terminal FD saved before dup2 redirect
    int pipe_stdout_write_fd;  // Pipe write-end FD saved after redirect (for restoring)
    int pipe_stderr_write_fd;  // Pipe write-end FD saved after redirect (for restoring)

    pthread_mutex_t lock;
    int is_initialized;
    int is_running;

    // Script completion sentinel synchronization
    pthread_mutex_t sentinel_mutex;
    pthread_cond_t sentinel_cond;
    volatile int sentinel_received;
} logging_state_t;

// Global logging state
static logging_state_t g_logging_state = {
    .log_tag = NULL,
    .logging_thread = 0,
    .thread_continue = 0,
    .stream_pfd = {{-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}},
    .native_loggers = NULL,
    .custom_outputs = NULL,
    .custom_output_count = 0,
    .interpreter_listeners = NULL,
    .interpreter_listener_count = 0,
    .original_stdout_fd = -1,
    .original_stderr_fd = -1,
    .pipe_stdout_write_fd = -1,
    .pipe_stderr_write_fd = -1,
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .is_initialized = 0,
    .is_running = 0,
    .sentinel_mutex = PTHREAD_MUTEX_INITIALIZER,
    .sentinel_cond = PTHREAD_COND_INITIALIZER,
    .sentinel_received = 0
};

// Thread-local error state
static __thread logging_error_t g_last_error = LOGGING_ERROR_NONE;

// ============================================================================
// Drain barrier queue
// ============================================================================
//
// FIFO queue of one-shot callbacks fired when LOGGING_DRAIN_BARRIER lines are
// observed on the VMLOGGER pipe. logging_drain_async() must atomically append
// to this queue AND write the barrier marker so pipe order matches queue
// order across concurrent callers — g_drain_mutex provides that atomicity.

typedef struct drain_node {
    logging_drain_cb_t cb;
    void* user_data;
    struct drain_node* next;
} drain_node_t;

static drain_node_t* g_drain_queue_head = NULL;
static drain_node_t* g_drain_queue_tail = NULL;
static pthread_mutex_t g_drain_mutex = PTHREAD_MUTEX_INITIALIZER;

// Pop the head node. Caller must hold g_drain_mutex. Returns NULL when empty.
static drain_node_t* drain_queue_pop_head_locked(void) {
    drain_node_t* node = g_drain_queue_head;
    if (node == NULL) return NULL;
    g_drain_queue_head = node->next;
    if (g_drain_queue_head == NULL) g_drain_queue_tail = NULL;
    return node;
}

// write() wrapper that retries on partial writes and EINTR. Returns 0 on
// success, -1 on error (errno preserved).
static int write_all_bytes(int fd, const void* buf, size_t count) {
    size_t total = 0;
    while (total < count) {
        ssize_t n = write(fd, (const char*)buf + total, count - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        total += (size_t)n;
    }
    return 0;
}

// ============================================================================
// Script completion sentinel synchronization
// ============================================================================

/**
 * Internal: Signal that the script completion sentinel has been received.
 * Called from the logging thread when the sentinel is detected in write_full_log_line.
 */
static void logging_signal_sentinel(void) {
    pthread_mutex_lock(&g_logging_state.sentinel_mutex);
    g_logging_state.sentinel_received = 1;
    pthread_cond_signal(&g_logging_state.sentinel_cond);
    pthread_mutex_unlock(&g_logging_state.sentinel_mutex);
}

void logging_reset_sentinel(void) {
    pthread_mutex_lock(&g_logging_state.sentinel_mutex);
    g_logging_state.sentinel_received = 0;
    pthread_mutex_unlock(&g_logging_state.sentinel_mutex);
}

int logging_wait_for_sentinel(int timeout_ms) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }

    pthread_mutex_lock(&g_logging_state.sentinel_mutex);
    int result = 0;
    while (!g_logging_state.sentinel_received && result == 0) {
        result = pthread_cond_timedwait(&g_logging_state.sentinel_cond,
                                         &g_logging_state.sentinel_mutex, &ts);
    }
    int received = g_logging_state.sentinel_received;
    pthread_mutex_unlock(&g_logging_state.sentinel_mutex);

    return received ? 0 : -1;
}

int logging_sentinel_received(void) {
    pthread_mutex_lock(&g_logging_state.sentinel_mutex);
    int received = g_logging_state.sentinel_received;
    pthread_mutex_unlock(&g_logging_state.sentinel_mutex);
    return received;
}

void logging_swap_listener(LogListener* dest, LogListener src) {
    if (!dest) return;
    /* Acquire the same lock that call_custom_logging_function holds for the
     * full callback dispatch — this serializes the swap with any in-flight
     * callback that's reading *dest, preventing UAF when the caller is about
     * to free a context referenced by the old listener. */
    pthread_mutex_lock(&g_logging_state.lock);
    *dest = src;
    pthread_mutex_unlock(&g_logging_state.lock);
}

int logging_drain_async(logging_drain_cb_t cb, void* user_data) {
    if (cb == NULL) return -1;

    drain_node_t* node = malloc(sizeof(drain_node_t));
    if (node == NULL) return -1;
    node->cb = cb;
    node->user_data = user_data;
    node->next = NULL;

    /* Snapshot the VMLOGGER write end without holding g_drain_mutex (a write()
     * under that lock would serialize all drains and could starve callers
     * if the pipe buffer fills). */
    pthread_mutex_lock(&g_logging_state.lock);
    int running = g_logging_state.is_running;
    int write_fd = running ? g_logging_state.stream_pfd[VMLOGGER_INDEX][1] : -1;
    pthread_mutex_unlock(&g_logging_state.lock);

    if (!running || write_fd < 0) {
        free(node);
        return -1;
    }

    /* g_drain_mutex provides FIFO ordering: enqueue and pipe-write must be
     * atomic relative to other concurrent drain_async callers, so the head
     * of the queue always matches the next barrier marker the logging
     * thread will read out of the pipe. */
    pthread_mutex_lock(&g_drain_mutex);
    if (g_drain_queue_tail == NULL) {
        g_drain_queue_head = g_drain_queue_tail = node;
    } else {
        g_drain_queue_tail->next = node;
        g_drain_queue_tail = node;
    }

    static const char marker[] = LOGGING_DRAIN_BARRIER "\n";
    int rc = write_all_bytes(write_fd, marker, sizeof(marker) - 1);
    pthread_mutex_unlock(&g_drain_mutex);

    if (rc != 0) {
        /* Pipe write failed — best-effort attempt to remove the node we just
         * enqueued so a future barrier doesn't dispatch a stale callback. */
        pthread_mutex_lock(&g_drain_mutex);
        drain_node_t** link = &g_drain_queue_head;
        while (*link != NULL && *link != node) link = &(*link)->next;
        if (*link == node) {
            *link = node->next;
            if (g_drain_queue_tail == node) g_drain_queue_tail = (*link == NULL) ? NULL : g_drain_queue_tail;
        }
        pthread_mutex_unlock(&g_drain_mutex);
        free(node);
        return -2;
    }
    return 0;
}

/* Sync wrapper helper state — stack-allocated by logging_drain(). */
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    int             done;
} drain_sync_t;

static void drain_sync_signal(void* user_data) {
    drain_sync_t* s = (drain_sync_t*)user_data;
    pthread_mutex_lock(&s->mutex);
    s->done = 1;
    pthread_cond_broadcast(&s->cond);
    pthread_mutex_unlock(&s->mutex);
}

int logging_drain(int timeout_ms) {
    drain_sync_t sync;
    pthread_mutex_init(&sync.mutex, NULL);
    pthread_cond_init(&sync.cond, NULL);
    sync.done = 0;

    int rc = logging_drain_async(drain_sync_signal, &sync);
    if (rc != 0) {
        pthread_cond_destroy(&sync.cond);
        pthread_mutex_destroy(&sync.mutex);
        return -2;  /* logging not running — also "nothing to drain" */
    }

    int result = 0;
    pthread_mutex_lock(&sync.mutex);
    if (timeout_ms <= 0) {
        while (!sync.done) pthread_cond_wait(&sync.cond, &sync.mutex);
    } else {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec  += timeout_ms / 1000;
        ts.tv_nsec += (timeout_ms % 1000) * 1000000L;
        if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
        int wrc = 0;
        while (!sync.done && wrc == 0) {
            wrc = pthread_cond_timedwait(&sync.cond, &sync.mutex, &ts);
        }
        if (!sync.done) result = -1;
    }
    pthread_mutex_unlock(&sync.mutex);

    pthread_cond_destroy(&sync.cond);
    pthread_mutex_destroy(&sync.mutex);
    return result;
}

// ============================================================================
// Async dispatch queue
// ============================================================================
//
// Custom-output callbacks, drain-barrier callbacks, and the script-completion
// sentinel run on a dedicated worker thread (g_dispatch_thread), NOT on the
// logger thread that drains the pipes. This decouples callback execution
// from pipe drainage so a slow or stdout-writing callback can never stall
// the pipe reader and deadlock the runtime.
//
// CALLBACK CONTRACT (see logging.h / log-listener.h): callbacks must NOT
// write to fd 1 / fd 2 (the redirected stdout/stderr). Use the platform's
// async log channel — __android_log_print / android.util.Log on Android,
// os_log on Apple, sd_journal_print on Linux/journald — those bypass the
// pipe. If a callback genuinely needs a "raw terminal" channel, write()
// directly to logging_get_original_stdout_fd() / _stderr_fd().
//
// A callback that violates the contract by printing to fd 1/2 will produce
// more pipe data, which the logger thread will read, which will enqueue
// another LOG_LINE, which the worker will dispatch — recursion bounded only
// by the queue cap. The bounded queue (DISPATCH_QUEUE_MAX_LOG_LINES) prevents
// runaway memory growth in that pathological case; LOG_LINE items are
// dropped on overflow with a periodic logcat warning. Control items
// (DRAIN_BARRIER, SENTINEL) bypass the bound — dropping them would break
// host/VM synchronization.

#define DISPATCH_QUEUE_MAX_LOG_LINES 1024

typedef enum {
    DISPATCH_LOG_LINE = 0,
    DISPATCH_DRAIN_BARRIER = 1,
    DISPATCH_SENTINEL = 2
} dispatch_item_type_t;

typedef struct dispatch_item {
    dispatch_item_type_t type;
    union {
        struct {
            char* line;        /* heap copy, freed after dispatch */
            log_stream_t stream;
            log_level_t level; /* severity attached at write_full_log_line time */
        } log_line;
        struct {
            logging_drain_cb_t cb;
            void* user_data;
        } drain;
        /* SENTINEL has no payload */
    } data;
    struct dispatch_item* next;
} dispatch_item_t;

static dispatch_item_t* g_dispatch_head = NULL;
static dispatch_item_t* g_dispatch_tail = NULL;
static int g_dispatch_log_line_count = 0;
static unsigned long g_dispatch_dropped_total = 0;
static int g_dispatch_thread_continue = 0;
static int g_dispatch_thread_started = 0;
static pthread_t g_dispatch_thread;
static pthread_mutex_t g_dispatch_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_dispatch_cond = PTHREAD_COND_INITIALIZER;

/* Caller MUST hold g_dispatch_mutex. Inserts at tail and signals one waiter. */
static void dispatch_enqueue_locked(dispatch_item_t* item) {
    item->next = NULL;
    if (g_dispatch_tail == NULL) {
        g_dispatch_head = g_dispatch_tail = item;
    } else {
        g_dispatch_tail->next = item;
        g_dispatch_tail = item;
    }
    if (item->type == DISPATCH_LOG_LINE) g_dispatch_log_line_count++;
    pthread_cond_signal(&g_dispatch_cond);
}

/* Try to enqueue a log line. Drops on backpressure. Bytes are copied. */
static void dispatch_try_enqueue_log_line(const char* line, log_stream_t stream, log_level_t level) {
    pthread_mutex_lock(&g_dispatch_mutex);
    if (g_dispatch_log_line_count >= DISPATCH_QUEUE_MAX_LOG_LINES) {
        unsigned long dropped = ++g_dispatch_dropped_total;
        pthread_mutex_unlock(&g_dispatch_mutex);
        /* Surface drops periodically (every 256th drop) so prolonged
         * backpressure is visible without flooding logcat. */
        if ((dropped & 0xFFu) == 1u) {
            jni_log_printf(JNI_LOG_WARN, g_logging_state.log_tag,
                           "Logging dispatch queue full; dropped %lu lines so far",
                           dropped);
        }
        return;
    }
    pthread_mutex_unlock(&g_dispatch_mutex);

    dispatch_item_t* item = malloc(sizeof(*item));
    if (item == NULL) return;
    item->type = DISPATCH_LOG_LINE;
    item->data.log_line.line = strdup(line);
    if (item->data.log_line.line == NULL) { free(item); return; }
    item->data.log_line.stream = stream;
    item->data.log_line.level = level;

    pthread_mutex_lock(&g_dispatch_mutex);
    dispatch_enqueue_locked(item);
    pthread_mutex_unlock(&g_dispatch_mutex);
}

/* Force-enqueue a drain barrier. On OOM, fire the cb inline (on the logger
 * thread) so synchronization doesn't hang — the cost is minor since OOM
 * here is a degenerate case. */
static void dispatch_enqueue_drain_barrier(logging_drain_cb_t cb, void* user_data) {
    dispatch_item_t* item = malloc(sizeof(*item));
    if (item == NULL) {
        if (cb != NULL) cb(user_data);
        return;
    }
    item->type = DISPATCH_DRAIN_BARRIER;
    item->data.drain.cb = cb;
    item->data.drain.user_data = user_data;

    pthread_mutex_lock(&g_dispatch_mutex);
    dispatch_enqueue_locked(item);
    pthread_mutex_unlock(&g_dispatch_mutex);
}

/* Force-enqueue a script-completion sentinel. On OOM, signal directly. */
static void dispatch_enqueue_sentinel(void) {
    dispatch_item_t* item = malloc(sizeof(*item));
    if (item == NULL) {
        logging_signal_sentinel();
        return;
    }
    item->type = DISPATCH_SENTINEL;

    pthread_mutex_lock(&g_dispatch_mutex);
    dispatch_enqueue_locked(item);
    pthread_mutex_unlock(&g_dispatch_mutex);
}

/* ============================================================================
 * Feedback-loop detection
 *
 * A custom_output callback that writes to fd 1 / fd 2 feeds bytes back into
 * the logging pipes that the dispatch thread is draining, so the next read
 * dispatches that same data right back. Without blocking writes (we set
 * O_NONBLOCK on the redirected fd, see create_and_redirect_stream) this is
 * a CPU-burning spin loop instead of a hard deadlock — but the operator
 * sees no diagnostic and the process just feels "stuck busy".
 *
 * We detect the spin pattern here: if the SAME line is dispatched more than
 * LOOP_DETECT_THRESHOLD times within LOOP_DETECT_WINDOW_MS, we write a
 * one-shot warning directly to the saved original stderr (bypassing the
 * pipes, so the warning itself can't feed the loop). Detection is purely
 * heuristic — a legitimate burst of identical lines will trip it once, then
 * stay quiet until the pattern breaks and resumes.
 *
 * Lives entirely on the dispatch thread, so no locking needed. */
#define LOOP_DETECT_SNAPSHOT_LEN 96
#define LOOP_DETECT_THRESHOLD    8
#define LOOP_DETECT_WINDOW_MS    500

typedef struct {
    char  snapshot[LOOP_DETECT_SNAPSHOT_LEN];
    int   snapshot_len;          /* 0 means "no current pattern" */
    int   repeat_count;
    long  window_start_ms;
    int   warning_emitted;       /* 1 once we've warned about this pattern */
} loop_detect_t;

static loop_detect_t g_loop_detect[NUM_STREAMS];

static long loop_detect_now_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (long)ts.tv_sec * 1000L + (long)ts.tv_nsec / 1000000L;
}

/* Map the public stream enum (1..5) to the internal 0-based array. Returns
 * -1 for unknown values so the caller can skip detection. */
static int loop_detect_stream_index(log_stream_t stream) {
    switch (stream) {
        case LOG_STREAM_RUBY_STDOUT:   return RUBY_STDOUT_INDEX;
        case LOG_STREAM_RUBY_STDERR:   return RUBY_STDERR_INDEX;
        case LOG_STREAM_VMLOGGER:      return VMLOGGER_INDEX;
        case LOG_STREAM_NATIVE_STDOUT: return NATIVE_STDOUT_INDEX;
        case LOG_STREAM_NATIVE_STDERR: return NATIVE_STDERR_INDEX;
    }
    return -1;
}

static const char* loop_detect_stream_name(log_stream_t stream) {
    switch (stream) {
        case LOG_STREAM_RUBY_STDOUT:   return "ruby_stdout";
        case LOG_STREAM_RUBY_STDERR:   return "ruby_stderr";
        case LOG_STREAM_VMLOGGER:      return "vmlogger";
        case LOG_STREAM_NATIVE_STDOUT: return "native_stdout";
        case LOG_STREAM_NATIVE_STDERR: return "native_stderr";
    }
    return "unknown";
}

static void detect_callback_feedback(log_stream_t stream, const char* line) {
    int idx = loop_detect_stream_index(stream);
    if (idx < 0) return;
    loop_detect_t* d = &g_loop_detect[idx];

    size_t line_len = line ? strlen(line) : 0;
    int snap_len = (int)(line_len < LOOP_DETECT_SNAPSHOT_LEN
                         ? line_len : LOOP_DETECT_SNAPSHOT_LEN);

    int same_as_last = (d->snapshot_len == snap_len) &&
                       (snap_len == 0 || memcmp(d->snapshot, line, (size_t)snap_len) == 0);

    long now = loop_detect_now_ms();

    if (same_as_last && d->snapshot_len > 0) {
        d->repeat_count++;
        if (!d->warning_emitted &&
            d->repeat_count >= LOOP_DETECT_THRESHOLD &&
            (now - d->window_start_ms) <= LOOP_DETECT_WINDOW_MS) {
            d->warning_emitted = 1;
            int err_fd = g_logging_state.original_stderr_fd;
            if (err_fd >= 0) {
                char warn[256];
                int n = snprintf(warn, sizeof(warn),
                    "[logging] possible callback feedback loop on stream %s: "
                    "same line dispatched %d times in <%ldms — "
                    "check that no log listener writes to fd 1/2\n",
                    loop_detect_stream_name(stream),
                    d->repeat_count,
                    (long)LOOP_DETECT_WINDOW_MS);
                if (n > 0) {
                    size_t to_write = (size_t)n < sizeof(warn) ? (size_t)n : sizeof(warn) - 1;
                    ssize_t ignored = write(err_fd, warn, to_write);
                    (void)ignored;
                }
            }
        }
    } else {
        memcpy(d->snapshot, line, (size_t)snap_len);
        d->snapshot_len     = snap_len;
        d->repeat_count     = 1;
        d->window_start_ms  = now;
        d->warning_emitted  = 0;
    }
}

/* Parse the in-band interpreter-id tag the Ruby-side TaggedIO emits at the
 * start of every line: "\x01[<decimal-id>]\x01<actual line>". On a match,
 * returns the parsed id (>0) and sets `*body_out` to point just past the
 * closing \x01. On no-match, returns LOG_NATIVE_INTERPRETER_ID and sets
 * `*body_out` to the original `line` pointer. The function does NOT mutate
 * `line` — the body pointer is an offset into the original buffer.
 *
 * SOH (\x01) was chosen because it never appears in normal Ruby script
 * output (control characters in source code emit as the byte not the
 * literal `\x01` escape) and survives utf-8 strings without ambiguity.
 *
 * Robustness: tolerates a missing/empty/non-numeric id by returning the
 * native-id fallback. This keeps a malformed tag from dropping the line
 * entirely. */
static int parse_interpreter_id_prefix(const char* line, const char** body_out) {
    *body_out = line;
    if (line == NULL || line[0] != '\x01' || line[1] != '[') return LOG_NATIVE_INTERPRETER_ID;
    const char* p = line + 2;
    int id = 0;
    int digits = 0;
    while (*p >= '0' && *p <= '9') {
        /* Cap at 9 digits so we can't overflow `id` even on adversarial
         * input — interpreter ids in practice are small ints from an
         * atomic_fetch_add counter. */
        if (digits >= 9) return LOG_NATIVE_INTERPRETER_ID;
        id = id * 10 + (*p - '0');
        digits++;
        p++;
    }
    if (digits == 0 || *p != ']' || *(p + 1) != '\x01') return LOG_NATIVE_INTERPRETER_ID;
    *body_out = p + 2;
    return id;
}

/* Look up the listener for [interpreter_id]; falls back to the head of
 * the registry (the first-registered listener, oldest = natural sink) if
 * no exact match. Returns 1 if a listener was found, 0 otherwise. Caller
 * holds g_logging_state.lock. */
static int find_interpreter_listener_locked(int interpreter_id, LogListener* out) {
    if (out == NULL) return 0;
    /* Native lines (id==NATIVE) and unmatched ids both route to the head
     * (oldest registration) — typically the long-running persistent
     * interpreter, which is the right sink for native printf output. */
    if (interpreter_id != LOG_NATIVE_INTERPRETER_ID) {
        for (interpreter_listener_node_t* n = g_logging_state.interpreter_listeners;
             n != NULL; n = n->next) {
            if (n->interpreter_id == interpreter_id) {
                *out = n->listener;
                return 1;
            }
        }
    }
    if (g_logging_state.interpreter_listeners != NULL) {
        *out = g_logging_state.interpreter_listeners->listener;
        return 1;
    }
    return 0;
}

/* Invoke all registered custom-output callbacks for one log line, AND
 * route the line to the per-interpreter listener identified by the line's
 * in-band tag (or the registry head for untagged native lines). Runs on
 * the dispatch worker thread; holds g_logging_state.lock for the duration
 * of the iteration to serialize against logging_add_custom_output /
 * logging_remove_custom_output / logging_register_interpreter_listener /
 * logging_unregister_interpreter_listener. */
static void dispatch_invoke_custom_callbacks(log_stream_t stream, log_level_t level, const char* line) {
    /* Strip the interpreter-id tag (if any) BEFORE feeding the line to
     * any downstream consumer — the tag is plumbing, not content. */
    const char* body = NULL;
    int interpreter_id = parse_interpreter_id_prefix(line, &body);
    /* detect_callback_feedback compares against last-seen lines; pass the
     * untagged body so the comparison is stable across taggings. */
    detect_callback_feedback(stream, body);

    pthread_mutex_lock(&g_logging_state.lock);

    /* Legacy custom_output callbacks: unchanged signature, no id. Kept
     * because non-interpreter consumers (logcat bridges, test harnesses)
     * still rely on the broadcast semantics. */
    custom_output_node_t* node = g_logging_state.custom_outputs;
    while (node != NULL) {
        if (node->func != NULL && node->context != NULL) {
            int err = node->func(body, stream, level, node->context);
            if (err != 0) {
                /* Surface the failure via thread-local error state.
                 * set_last_error() is defined later in the file, so assign
                 * directly here to avoid a forward-declaration. */
                g_last_error = LOGGING_ERROR_CUSTOM_CALLBACK_FAILED;
            }
        }
        node = node->next;
    }

    /* Per-interpreter routing: hand the line to exactly one listener,
     * identified by the in-band tag. Lookup happens under the same lock
     * so an unregister can't race the dispatch and free the listener
     * context while we're holding a copy. */
    LogListener listener;
    log_listener_init(&listener);
    if (find_interpreter_listener_locked(interpreter_id, &listener) &&
        listener.on_log_message != NULL) {
        listener.on_log_message(&listener, body, stream, level, interpreter_id);
    }

    pthread_mutex_unlock(&g_logging_state.lock);
}

/* Worker thread: pure callback dispatcher. Reads the queue, invokes the
 * appropriate callback per item, never touches any pipe FD. */
static void* dispatch_thread_function(void* arg) {
    (void)arg;

    for (;;) {
        pthread_mutex_lock(&g_dispatch_mutex);
        while (g_dispatch_head == NULL && g_dispatch_thread_continue) {
            pthread_cond_wait(&g_dispatch_cond, &g_dispatch_mutex);
        }
        if (g_dispatch_head == NULL) {
            /* Empty + shutdown requested */
            pthread_mutex_unlock(&g_dispatch_mutex);
            break;
        }
        dispatch_item_t* item = g_dispatch_head;
        g_dispatch_head = item->next;
        if (g_dispatch_head == NULL) g_dispatch_tail = NULL;
        if (item->type == DISPATCH_LOG_LINE) g_dispatch_log_line_count--;
        pthread_mutex_unlock(&g_dispatch_mutex);

        switch (item->type) {
            case DISPATCH_LOG_LINE:
                dispatch_invoke_custom_callbacks(item->data.log_line.stream,
                                                 item->data.log_line.level,
                                                 item->data.log_line.line);
                free(item->data.log_line.line);
                break;
            case DISPATCH_DRAIN_BARRIER:
                if (item->data.drain.cb != NULL) {
                    item->data.drain.cb(item->data.drain.user_data);
                }
                break;
            case DISPATCH_SENTINEL:
                logging_signal_sentinel();
                break;
        }
        free(item);
    }

    return NULL;
}

/* Start the dispatch worker. Idempotent. */
static int dispatch_thread_start(void) {
    pthread_mutex_lock(&g_dispatch_mutex);
    if (g_dispatch_thread_started) {
        pthread_mutex_unlock(&g_dispatch_mutex);
        return 0;
    }
    g_dispatch_thread_continue = 1;
    pthread_mutex_unlock(&g_dispatch_mutex);

    if (pthread_create(&g_dispatch_thread, NULL, dispatch_thread_function, NULL) != 0) {
        pthread_mutex_lock(&g_dispatch_mutex);
        g_dispatch_thread_continue = 0;
        pthread_mutex_unlock(&g_dispatch_mutex);
        return -1;
    }

    pthread_mutex_lock(&g_dispatch_mutex);
    g_dispatch_thread_started = 1;
    pthread_mutex_unlock(&g_dispatch_mutex);
    return 0;
}

/* Stop the dispatch worker. The worker drains all currently-queued items
 * before exiting (the wait loop only blocks when the queue is empty). Must
 * be called AFTER the logger thread has joined so no new items can be
 * enqueued while/after we shut down. */
static void dispatch_thread_stop(void) {
    pthread_mutex_lock(&g_dispatch_mutex);
    if (!g_dispatch_thread_started) {
        pthread_mutex_unlock(&g_dispatch_mutex);
        return;
    }
    g_dispatch_thread_continue = 0;
    pthread_cond_broadcast(&g_dispatch_cond);
    pthread_mutex_unlock(&g_dispatch_mutex);

    pthread_join(g_dispatch_thread, NULL);

    pthread_mutex_lock(&g_dispatch_mutex);
    g_dispatch_thread_started = 0;
    /* Defensive cleanup: free any items still queued. Should be empty since
     * the worker drains before exiting and the logger has joined. */
    while (g_dispatch_head != NULL) {
        dispatch_item_t* it = g_dispatch_head;
        g_dispatch_head = it->next;
        if (it->type == DISPATCH_LOG_LINE) free(it->data.log_line.line);
        free(it);
    }
    g_dispatch_tail = NULL;
    g_dispatch_log_line_count = 0;
    pthread_mutex_unlock(&g_dispatch_mutex);
}

/**
 * Internal: Set the last error for the current thread
 */
static inline void set_last_error(logging_error_t error) {
    g_last_error = error;
}

/**
 * Get the last error that occurred in the current thread
 */
logging_error_t logging_get_last_error(void) {
    return g_last_error;
}

/**
 * Clear the last error for the current thread
 */
void logging_clear_last_error(void) {
    g_last_error = LOGGING_ERROR_NONE;
}

/**
 * Get a human-readable description of an error code
 */
const char* logging_error_string(logging_error_t error) {
    switch (error) {
        case LOGGING_ERROR_NONE:
            return "No error";

        /* Initialization errors */
        case LOGGING_ERROR_NOT_INITIALIZED:
            return "Logging system not initialized";
        case LOGGING_ERROR_ALREADY_INITIALIZED:
            return "Logging system already initialized";
        case LOGGING_ERROR_INVALID_PARAMETER:
            return "Invalid parameter";
        case LOGGING_ERROR_MEMORY_ALLOCATION:
            return "Memory allocation failed";

        /* Stream redirection errors */
        case LOGGING_ERROR_SOCKETPAIR_FAILED:
            return "Failed to create socketpair";
        case LOGGING_ERROR_DUP2_FAILED:
            return "Failed to duplicate file descriptor (dup2)";
        case LOGGING_ERROR_STDOUT_REDIRECT_FAILED:
            return "Failed to redirect stdout";
        case LOGGING_ERROR_STDERR_REDIRECT_FAILED:
            return "Failed to redirect stderr";

        /* Thread errors */
        case LOGGING_ERROR_THREAD_CREATE_FAILED:
            return "Failed to create logging thread";
        case LOGGING_ERROR_THREAD_JOIN_FAILED:
            return "Failed to join logging thread";
        case LOGGING_ERROR_THREAD_ALREADY_RUNNING:
            return "Logging thread already running";

        /* I/O errors */
        case LOGGING_ERROR_READ_FAILED:
            return "Read operation failed";
        case LOGGING_ERROR_WRITE_FAILED:
            return "Write operation failed";
        case LOGGING_ERROR_SELECT_FAILED:
            return "Select operation failed";

        /* Callback errors */
        case LOGGING_ERROR_NATIVE_CALLBACK_FAILED:
            return "Native logging callback failed";
        case LOGGING_ERROR_CUSTOM_CALLBACK_FAILED:
            return "Custom logging callback failed";
        case LOGGING_ERROR_CALLBACK_NOT_FOUND:
            return "Callback not found";
        case LOGGING_ERROR_CALLBACK_ALREADY_EXISTS:
            return "Callback already exists";

        /* State errors */
        case LOGGING_ERROR_NOT_RUNNING:
            return "Logging thread not running";
        case LOGGING_ERROR_MUTEX_LOCK_FAILED:
            return "Mutex lock operation failed";

        default:
            return "Unknown error";
    }
}

/**
 * Default weak implementation of platform-specific logging setup.
 * Platform-specific modules (e.g., Android) can provide a strong symbol to override this.
 */
__attribute__((weak))
void logging_setup_platform_native(void) {
    // Default no-op implementation
    // Platform-specific code can override this with a strong symbol
}

/**
 * Write log message to all native logging systems
 * Returns 0 on success, negative error code if any callback fails
 * Sets thread-local last error to the first failure encountered
 */
static int call_native_logging_function(int prio, const char* tag, const char* text) {
    pthread_mutex_lock(&g_logging_state.lock);

    native_logger_node_t* current = g_logging_state.native_loggers;
    int error = 0;
    int first_error_code = 0;

    while (current != NULL) {
        if (current->func != NULL) {
            int callback_error = current->func(prio, tag, text);
            if (callback_error != 0) {
                // Store first error for detailed reporting
                if (first_error_code == 0) {
                    first_error_code = callback_error;
                    set_last_error(LOGGING_ERROR_NATIVE_CALLBACK_FAILED);
                }
                // Accumulate all errors (preserves existing OR behavior)
                error |= callback_error;
            }
        }
        current = current->next;
    }

    pthread_mutex_unlock(&g_logging_state.lock);
    return error;
}

/* Custom-output callback dispatch lives on the worker thread now — see
 * dispatch_invoke_custom_callbacks() above. The previous synchronous
 * implementation here used a process-global dup2 swap on STDOUT_FILENO /
 * STDERR_FILENO around each callback to prevent feedback loops; that swap
 * raced with concurrent writes from the script thread (the script's
 * write(2) could land between the swap-out and swap-back, sending bytes to
 * /dev/null). The async-dispatch design eliminates the race entirely:
 * callbacks run off the logger thread, and the contract requires they not
 * write to fd 1/2 — so no swap is needed. */

/* Map a log stream to a default severity. Used for every non-VMLOGGER line
 * (where the FD identity carries the only severity signal), and as a fallback
 * for VMLOGGER lines that arrive without a recognizable level tag — e.g. a
 * legacy embedder writing directly into the VMLOGGER pipe, or transitional
 * builds where the C side has been upgraded but the Ruby script_runner has
 * not. The fallback is INFO rather than UNKNOWN so consumers can always treat
 * the field as set. */
static log_level_t derive_default_level_for_stream(log_stream_t stream) {
    switch (stream) {
        case LOG_STREAM_RUBY_STDERR:
        case LOG_STREAM_NATIVE_STDERR:
            return LOG_LEVEL_ERROR;
        case LOG_STREAM_RUBY_STDOUT:
        case LOG_STREAM_NATIVE_STDOUT:
        case LOG_STREAM_VMLOGGER:
        default:
            return LOG_LEVEL_INFO;
    }
}

/* Parse a VMLogger severity tag emitted by safe_runner.rb (see
 * VMLOGGER_LEVEL_TAG_* in constants.h). On match, sets *out_level to the
 * parsed severity and returns a pointer to the byte immediately past the
 * tag (i.e. the start of the actual message). On no-match the input pointer
 * is returned unchanged and *out_level is left untouched, so callers can
 * safely seed it with derive_default_level_for_stream() beforehand. */
static const char* parse_vmlogger_level_tag(const char* line, log_level_t* out_level) {
    static const size_t prefix_len = sizeof(VMLOGGER_LEVEL_TAG_PREFIX) - 1;
    static const size_t suffix_len = sizeof(VMLOGGER_LEVEL_TAG_SUFFIX) - 1;

    if (strncmp(line, VMLOGGER_LEVEL_TAG_PREFIX, prefix_len) != 0) {
        return line;
    }
    const char* name = line + prefix_len;
    const char* suffix = strstr(name, VMLOGGER_LEVEL_TAG_SUFFIX);
    if (suffix == NULL) {
        return line;
    }
    size_t name_len = (size_t)(suffix - name);
    if (name_len == sizeof(VMLOGGER_LEVEL_NAME_DEBUG) - 1
        && memcmp(name, VMLOGGER_LEVEL_NAME_DEBUG, name_len) == 0) {
        *out_level = LOG_LEVEL_DEBUG;
    } else if (name_len == sizeof(VMLOGGER_LEVEL_NAME_INFO) - 1
               && memcmp(name, VMLOGGER_LEVEL_NAME_INFO, name_len) == 0) {
        *out_level = LOG_LEVEL_INFO;
    } else if (name_len == sizeof(VMLOGGER_LEVEL_NAME_ERROR) - 1
               && memcmp(name, VMLOGGER_LEVEL_NAME_ERROR, name_len) == 0) {
        *out_level = LOG_LEVEL_ERROR;
    } else {
        /* Unknown level inside a well-formed tag — keep the original line
         * intact rather than swallow data the host cannot interpret. */
        return line;
    }
    return suffix + suffix_len;
}

/* Pick the native logging priority (Android logcat / android.util.Log style)
 * matching a parsed log_level_t. Kept narrow on purpose: VMLogger only emits
 * DEBUG/INFO/ERROR, and every other source path collapses to one of those
 * via derive_default_level_for_stream(). */
static int native_priority_for_level(log_level_t level) {
    switch (level) {
        case LOG_LEVEL_DEBUG: return LOG_DEBUG;
        case LOG_LEVEL_ERROR: return LOG_ERROR;
        case LOG_LEVEL_INFO:
        default:              return LOG_INFO;
    }
}

/**
 * Output a complete log line to all configured outputs
 * Returns 0 on success, negative if any output fails
 */
static int write_full_log_line(const char* line, log_stream_t stream) {
    /* Intercept the script-completion sentinel. Forwarded to the dispatch
     * worker as a control item so it fires AFTER all preceding LOG_LINE
     * items have been delivered to callbacks (FIFO ordering preserved).
     * Checked before the VMLOG-level tag parser since safe_runner.rb's
     * VMLogger.protocol bypasses tagging — the sentinel rides the pipe raw. */
    if (stream == LOG_STREAM_VMLOGGER && strstr(line, SCRIPT_COMPLETE_SENTINEL) != NULL) {
        dispatch_enqueue_sentinel();
        return 0;
    }

    /* Drain barrier: pop the head of the FIFO drain queue and forward its
     * callback to the dispatch worker, where it fires AFTER all preceding
     * LOG_LINE items. No FD swap — drain callbacks run on the worker, not
     * the logger thread, and callbacks must follow the no-stdout/stderr
     * contract documented in logging.h. */
    if (stream == LOG_STREAM_VMLOGGER && strstr(line, LOGGING_DRAIN_BARRIER) != NULL) {
        pthread_mutex_lock(&g_drain_mutex);
        drain_node_t* node = drain_queue_pop_head_locked();
        pthread_mutex_unlock(&g_drain_mutex);

        if (node != NULL) {
            dispatch_enqueue_drain_barrier(node->cb, node->user_data);
            free(node);
        }
        return 0;
    }

    /* Recover the per-VMLogger-call severity that the single pipe would
     * otherwise flatten. For non-VMLOGGER streams the default level (derived
     * from the FD identity) is already correct and parse_* is skipped. */
    log_level_t level = derive_default_level_for_stream(stream);
    if (stream == LOG_STREAM_VMLOGGER) {
        line = parse_vmlogger_level_tag(line, &level);
    }

    const char* tag = (g_logging_state.log_tag != NULL) ? g_logging_state.log_tag : "UNKNOWN";
    int priority = native_priority_for_level(level);

    /* Strip the interpreter-id tag before handing the line to anything
     * downstream. The tag is plumbing for per-interpreter routing — it
     * has no business showing up in logcat or in any consumer's log file.
     * `parse_interpreter_id_prefix` does not mutate the input; it returns
     * the id (used by dispatch_invoke_custom_callbacks for registry
     * lookup) and a pointer past the prefix in `body`. We pass `body` to
     * BOTH the native logger and the async dispatch queue — that way:
     *   - logcat shows the unprefixed text the Ruby code actually wrote,
     *   - the dispatch worker re-parses the same prefix (cheap) to route
     *     to the per-interpreter listener (see dispatch_invoke_custom_callbacks).
     * We must re-enqueue the original tagged `line` though, because the
     * dispatch path needs the tag to find the right interpreter listener;
     * stripping it here would lose the routing key. */
    const char* native_body = NULL;
    (void) parse_interpreter_id_prefix(line, &native_body);

    /* Native logging (logcat / __android_log_print etc.) is fast and writes
     * to a dedicated platform channel that does not feed back into our
     * pipes, so we keep it inline on the logger thread. Custom callbacks
     * may be slow and may be implemented in JNI/Kotlin/JVM, so they're
     * dispatched asynchronously. */
    int native_logging_error = call_native_logging_function(priority, tag, native_body);
    if (native_logging_error != 0) {
        jni_log_printf(JNI_LOG_ERROR, g_logging_state.log_tag,
                       "write_full_log_line: Native logging callback failed (error %d)",
                       native_logging_error);
    }

    dispatch_try_enqueue_log_line(line, stream, level);

    return native_logging_error;
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
        set_last_error(LOGGING_ERROR_MEMORY_ALLOCATION);
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

    // Block all signals to prevent Ruby's GC signal handlers (SIGPROF/SIGALRM)
    // from being delivered to this thread. While the logging thread doesn't
    // interact with Ruby objects, blocking signals is defense-in-depth.
    sigset_t set;
    sigfillset(&set);
    pthread_sigmask(SIG_BLOCK, &set, NULL);

    stream_buffer_t streams[NUM_STREAMS];

    // Initialize all stream buffers
    if (init_stream_buffer(&streams[RUBY_STDOUT_INDEX], LOG_STREAM_RUBY_STDOUT, g_logging_state.stream_pfd[RUBY_STDOUT_INDEX][0]) != 0 ||
        init_stream_buffer(&streams[RUBY_STDERR_INDEX], LOG_STREAM_RUBY_STDERR, g_logging_state.stream_pfd[RUBY_STDERR_INDEX][0]) != 0 ||
        init_stream_buffer(&streams[VMLOGGER_INDEX], LOG_STREAM_VMLOGGER, g_logging_state.stream_pfd[VMLOGGER_INDEX][0]) != 0 ||
        init_stream_buffer(&streams[NATIVE_STDOUT_INDEX], LOG_STREAM_NATIVE_STDOUT, g_logging_state.stream_pfd[NATIVE_STDOUT_INDEX][0]) != 0 ||
        init_stream_buffer(&streams[NATIVE_STDERR_INDEX], LOG_STREAM_NATIVE_STDERR, g_logging_state.stream_pfd[NATIVE_STDERR_INDEX][0]) != 0) {
        set_last_error(LOGGING_ERROR_MEMORY_ALLOCATION);
        call_native_logging_function(LOG_ERROR, g_logging_state.log_tag, "Failed to allocate buffers, aborting logging thread");
        return NULL;
    }

    // Find max fd for select()
    int max_fd = streams[RUBY_STDOUT_INDEX].fd;
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
            set_last_error(LOGGING_ERROR_SELECT_FAILED);
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
                    set_last_error(LOGGING_ERROR_READ_FAILED);
                    const char* stream_name;
                    switch (i) {
                        case RUBY_STDOUT_INDEX: stream_name = "ruby_stdout"; break;
                        case RUBY_STDERR_INDEX: stream_name = "ruby_stderr"; break;
                        case VMLOGGER_INDEX: stream_name = "vmlogger"; break;
                        case NATIVE_STDOUT_INDEX: stream_name = "native_stdout"; break;
                        case NATIVE_STDERR_INDEX: stream_name = "native_stderr"; break;
                        default: stream_name = "unknown"; break;
                    }
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
    // Check if we still have callbacks before flushing to avoid use-after-free
    pthread_mutex_lock(&g_logging_state.lock);
    int has_outputs = (g_logging_state.custom_output_count > 0 || g_logging_state.native_loggers != NULL);
    pthread_mutex_unlock(&g_logging_state.lock);

    for (int i = 0; i < NUM_STREAMS; i++) {
        // Only flush if we still have active output callbacks
        // Otherwise we risk use-after-free during shutdown
        if (has_outputs) {
            send_stream_buffer_to_output_as_line(&streams[i]);
        }
        free_stream_buffer(&streams[i]);
    }

    return NULL;
}


/**
 * Create a socketpair and redirect a file descriptor
 * Note: This is called from internal_start_logging_thread which holds the mutex
 */
static int create_and_redirect_stream(int stream_index, int target_fd) {
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, g_logging_state.stream_pfd[stream_index]) == -1) {
        set_last_error(LOGGING_ERROR_SOCKETPAIR_FAILED);
        // Note: Cannot call call_native_logging_function here - called with mutex held
        return -1;
    }

    if (dup2(g_logging_state.stream_pfd[stream_index][1], target_fd) == -1) {
        set_last_error(LOGGING_ERROR_DUP2_FAILED);
        // Note: Cannot call call_native_logging_function here - called with mutex held
        return -1;
    }

    /* Make the redirected fd non-blocking so a misbehaving log callback that
     * writes back to fd 1/2 can't hard-deadlock the process. With blocking
     * semantics, a large enough write from inside a callback fills the
     * socketpair send buffer; the callback blocks in write() waiting for the
     * reader, which is the same dispatch thread that just invoked it ->
     * permanent hang. With O_NONBLOCK, the offending write gets EAGAIN /
     * short-write under buffer pressure: the data is lost, but the process
     * keeps running. The dispatch loop additionally runs feedback-loop
     * detection (see detect_callback_feedback below) so the operator sees a
     * clear warning instead of mysterious CPU spin. */
    int flags = fcntl(target_fd, F_GETFL, 0);
    if (flags >= 0) {
        (void)fcntl(target_fd, F_SETFL, flags | O_NONBLOCK);
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
        set_last_error(LOGGING_ERROR_THREAD_ALREADY_RUNNING);
        return 0; // Already running
    }

    if (!g_logging_state.is_initialized) {
        set_last_error(LOGGING_ERROR_NOT_INITIALIZED);
        // Note: Cannot call call_native_logging_function here as this function
        // is called with the mutex already held (from logging_add_custom_output)
        return LOGGING_ERROR_NOT_INITIALIZED;
    }

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    // Create socketpairs for all streams
    // Only redirect native stdout/stderr to actual STDOUT_FILENO/STDERR_FILENO
    // Ruby streams and VMLogger get their own FDs that can be retrieved via logging_get_stream_fd()

    // Ruby stdout - just create socketpair, no redirect
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, g_logging_state.stream_pfd[RUBY_STDOUT_INDEX]) == -1) {
        set_last_error(LOGGING_ERROR_SOCKETPAIR_FAILED);
        cleanup_streams();
        return LOGGING_ERROR_SOCKETPAIR_FAILED;
    }

    // Ruby stderr - just create socketpair, no redirect
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, g_logging_state.stream_pfd[RUBY_STDERR_INDEX]) == -1) {
        set_last_error(LOGGING_ERROR_SOCKETPAIR_FAILED);
        cleanup_streams();
        return LOGGING_ERROR_SOCKETPAIR_FAILED;
    }

    // VMLogger - just create socketpair, no redirect
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, g_logging_state.stream_pfd[VMLOGGER_INDEX]) == -1) {
        set_last_error(LOGGING_ERROR_SOCKETPAIR_FAILED);
        cleanup_streams();
        return LOGGING_ERROR_SOCKETPAIR_FAILED;
    }

    // Save original stdout/stderr before redirecting.
    // Callbacks use these to write output without feeding back into the logging pipe.
    g_logging_state.original_stdout_fd = dup(STDOUT_FILENO);
    g_logging_state.original_stderr_fd = dup(STDERR_FILENO);
    if (g_logging_state.original_stdout_fd == -1 || g_logging_state.original_stderr_fd == -1) {
        set_last_error(LOGGING_ERROR_DUP2_FAILED);
        if (g_logging_state.original_stdout_fd != -1) close(g_logging_state.original_stdout_fd);
        if (g_logging_state.original_stderr_fd != -1) close(g_logging_state.original_stderr_fd);
        g_logging_state.original_stdout_fd = -1;
        g_logging_state.original_stderr_fd = -1;
        cleanup_streams();
        return LOGGING_ERROR_DUP2_FAILED;
    }

    // Native stdout - redirect actual stdout
    if (create_and_redirect_stream(NATIVE_STDOUT_INDEX, STDOUT_FILENO) != 0) {
        set_last_error(LOGGING_ERROR_STDOUT_REDIRECT_FAILED);
        close(g_logging_state.original_stdout_fd);
        close(g_logging_state.original_stderr_fd);
        g_logging_state.original_stdout_fd = -1;
        g_logging_state.original_stderr_fd = -1;
        cleanup_streams();
        return LOGGING_ERROR_STDOUT_REDIRECT_FAILED;
    }

    // Native stderr - redirect actual stderr
    if (create_and_redirect_stream(NATIVE_STDERR_INDEX, STDERR_FILENO) != 0) {
        set_last_error(LOGGING_ERROR_STDERR_REDIRECT_FAILED);
        close(g_logging_state.original_stdout_fd);
        close(g_logging_state.original_stderr_fd);
        g_logging_state.original_stdout_fd = -1;
        g_logging_state.original_stderr_fd = -1;
        cleanup_streams();
        return LOGGING_ERROR_STDERR_REDIRECT_FAILED;
    }

    // Save pipe write-end FDs so we can restore the redirect after callbacks.
    // After dup2, STDOUT_FILENO/STDERR_FILENO point to the pipe write ends.
    g_logging_state.pipe_stdout_write_fd = dup(STDOUT_FILENO);
    g_logging_state.pipe_stderr_write_fd = dup(STDERR_FILENO);
    if (g_logging_state.pipe_stdout_write_fd == -1 || g_logging_state.pipe_stderr_write_fd == -1) {
        set_last_error(LOGGING_ERROR_DUP2_FAILED);
        if (g_logging_state.pipe_stdout_write_fd != -1) close(g_logging_state.pipe_stdout_write_fd);
        if (g_logging_state.pipe_stderr_write_fd != -1) close(g_logging_state.pipe_stderr_write_fd);
        g_logging_state.pipe_stdout_write_fd = -1;
        g_logging_state.pipe_stderr_write_fd = -1;
        close(g_logging_state.original_stdout_fd);
        close(g_logging_state.original_stderr_fd);
        g_logging_state.original_stdout_fd = -1;
        g_logging_state.original_stderr_fd = -1;
        cleanup_streams();
        return LOGGING_ERROR_DUP2_FAILED;
    }

    // Start the dispatch worker first so it's ready to receive items the
    // moment the logger thread starts producing them.
    if (dispatch_thread_start() != 0) {
        set_last_error(LOGGING_ERROR_THREAD_CREATE_FAILED);
        close(g_logging_state.original_stdout_fd);
        close(g_logging_state.original_stderr_fd);
        close(g_logging_state.pipe_stdout_write_fd);
        close(g_logging_state.pipe_stderr_write_fd);
        g_logging_state.original_stdout_fd = -1;
        g_logging_state.original_stderr_fd = -1;
        g_logging_state.pipe_stdout_write_fd = -1;
        g_logging_state.pipe_stderr_write_fd = -1;
        cleanup_streams();
        return LOGGING_ERROR_THREAD_CREATE_FAILED;
    }

    // Start logging thread
    g_logging_state.thread_continue = 1;
    if (pthread_create(&g_logging_state.logging_thread, NULL, logging_function_thread, NULL) != 0) {
        set_last_error(LOGGING_ERROR_THREAD_CREATE_FAILED);
        // Note: Cannot call call_native_logging_function here - called with mutex held
        dispatch_thread_stop();
        close(g_logging_state.original_stdout_fd);
        close(g_logging_state.original_stderr_fd);
        close(g_logging_state.pipe_stdout_write_fd);
        close(g_logging_state.pipe_stderr_write_fd);
        g_logging_state.original_stdout_fd = -1;
        g_logging_state.original_stderr_fd = -1;
        g_logging_state.pipe_stdout_write_fd = -1;
        g_logging_state.pipe_stderr_write_fd = -1;
        cleanup_streams();
        return LOGGING_ERROR_THREAD_CREATE_FAILED;
    }

    g_logging_state.is_running = 1;
    // Note: Cannot call call_native_logging_function here - called with mutex held
    return 0;
}

/**
 * Internal: Stop the logging thread gracefully
 * Must be called with lock held
 */
static int internal_stop_logging_thread(void) {
    if (!g_logging_state.is_running) {
        set_last_error(LOGGING_ERROR_NOT_RUNNING);
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

    /* Stop the dispatch worker AFTER the logger thread has joined so no
     * new items can be enqueued during shutdown. The worker drains all
     * already-queued items before exiting. Done outside g_logging_state.lock
     * because the worker takes that lock during callback iteration — holding
     * it here would deadlock. */
    if (result == 0) {
        dispatch_thread_stop();
    }

    pthread_mutex_lock(&g_logging_state.lock);

    if (result != 0) {
        set_last_error(LOGGING_ERROR_THREAD_JOIN_FAILED);
        // Note: Cannot call call_native_logging_function here - called with mutex held
        return LOGGING_ERROR_THREAD_JOIN_FAILED;
    }

    g_logging_state.logging_thread = 0;
    g_logging_state.is_running = 0;

    // Flush stdio buffers while stdout/stderr still point to the logging pipe.
    // Without this, buffered data written before shutdown would either be lost
    // (pipe write-end closed) or bypass the logging thread (flushed after dup2
    // restores the original FDs).
    fflush(stdout);
    fflush(stderr);

    // Restore stdout/stderr to the original terminal FDs before closing anything.
    // Without this, STDOUT_FILENO/STDERR_FILENO still point to the (now dead) pipe
    // write-ends, and any subsequent printf/println would hit a bad FD or SIGPIPE.
    if (g_logging_state.original_stdout_fd != -1) {
        dup2(g_logging_state.original_stdout_fd, STDOUT_FILENO);
    }
    if (g_logging_state.original_stderr_fd != -1) {
        dup2(g_logging_state.original_stderr_fd, STDERR_FILENO);
    }

    // Close saved FDs
    if (g_logging_state.original_stdout_fd != -1) {
        close(g_logging_state.original_stdout_fd);
        g_logging_state.original_stdout_fd = -1;
    }
    if (g_logging_state.original_stderr_fd != -1) {
        close(g_logging_state.original_stderr_fd);
        g_logging_state.original_stderr_fd = -1;
    }
    if (g_logging_state.pipe_stdout_write_fd != -1) {
        close(g_logging_state.pipe_stdout_write_fd);
        g_logging_state.pipe_stdout_write_fd = -1;
    }
    if (g_logging_state.pipe_stderr_write_fd != -1) {
        close(g_logging_state.pipe_stderr_write_fd);
        g_logging_state.pipe_stderr_write_fd = -1;
    }

    return 0;
}

/**
 * Initialize the logging system
 */
int logging_init(const char* appname) {
    if (appname == NULL) {
        set_last_error(LOGGING_ERROR_INVALID_PARAMETER);
        return LOGGING_ERROR_INVALID_PARAMETER;
    }

    pthread_mutex_lock(&g_logging_state.lock);

    if (g_logging_state.is_initialized) {
        set_last_error(LOGGING_ERROR_ALREADY_INITIALIZED);
        pthread_mutex_unlock(&g_logging_state.lock);
        return 0; // Already initialized
    }

    g_logging_state.log_tag = strdup(appname);
    if (g_logging_state.log_tag == NULL) {
        set_last_error(LOGGING_ERROR_MEMORY_ALLOCATION);
        pthread_mutex_unlock(&g_logging_state.lock);
        // Note: Cannot call call_native_logging_function here as it would try to lock the mutex
        // that we just unlocked, and we're in an error path during initialization.
        return LOGGING_ERROR_MEMORY_ALLOCATION;
    }

    g_logging_state.is_initialized = 1;

    pthread_mutex_unlock(&g_logging_state.lock);

    // Setup platform-specific native logging (e.g., Android logcat)
    // This is called AFTER initialization completes so platform code can safely
    // call logging_add_native_function() without holding the lock.
    logging_setup_platform_native();

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

    // Free all per-interpreter listeners. In a well-behaved client every
    // ruby_interpreter_create has a matching destroy before we get here, so
    // this is normally empty; loop is purely defensive against leaks.
    interpreter_listener_node_t* interp_current = g_logging_state.interpreter_listeners;
    while (interp_current != NULL) {
        interpreter_listener_node_t* next = interp_current->next;
        free(interp_current);
        interp_current = next;
    }
    g_logging_state.interpreter_listeners = NULL;
    g_logging_state.interpreter_listener_count = 0;

    // Free log tag
    if (g_logging_state.log_tag != NULL) {
        free(g_logging_state.log_tag);
        g_logging_state.log_tag = NULL;
    }

    g_logging_state.is_initialized = 0;

    pthread_mutex_unlock(&g_logging_state.lock);

    /* Fire any still-pending drain callbacks so synchronous waiters unblock
     * — at shutdown the system is, by definition, drained. Done outside the
     * logging lock since callbacks may be arbitrary code. */
    pthread_mutex_lock(&g_drain_mutex);
    drain_node_t* drain_current = g_drain_queue_head;
    g_drain_queue_head = g_drain_queue_tail = NULL;
    pthread_mutex_unlock(&g_drain_mutex);
    while (drain_current != NULL) {
        drain_node_t* next = drain_current->next;
        if (drain_current->cb != NULL) drain_current->cb(drain_current->user_data);
        free(drain_current);
        drain_current = next;
    }

    return 0;
}

/**
 * Add a native logging function
 */
int logging_add_native_function(logging_native_logging_func_t func) {
    if (func == NULL) {
        set_last_error(LOGGING_ERROR_INVALID_PARAMETER);
        return LOGGING_ERROR_INVALID_PARAMETER;
    }

    pthread_mutex_lock(&g_logging_state.lock);

    // Check if already exists
    native_logger_node_t* current = g_logging_state.native_loggers;
    while (current != NULL) {
        if (current->func == func) {
            set_last_error(LOGGING_ERROR_CALLBACK_ALREADY_EXISTS);
            pthread_mutex_unlock(&g_logging_state.lock);
            return 0; // Already added
        }
        current = current->next;
    }

    // Create new node
    native_logger_node_t* new_node = (native_logger_node_t*)malloc(sizeof(native_logger_node_t));
    if (new_node == NULL) {
        set_last_error(LOGGING_ERROR_MEMORY_ALLOCATION);
        pthread_mutex_unlock(&g_logging_state.lock);
        return LOGGING_ERROR_MEMORY_ALLOCATION;
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
        set_last_error(LOGGING_ERROR_INVALID_PARAMETER);
        return LOGGING_ERROR_INVALID_PARAMETER;
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

    set_last_error(LOGGING_ERROR_CALLBACK_NOT_FOUND);
    pthread_mutex_unlock(&g_logging_state.lock);
    return LOGGING_ERROR_CALLBACK_NOT_FOUND;
}

/**
 * Add a custom output callback
 */
int logging_add_custom_output(logging_custom_output_func_t func, void* context) {
    if (func == NULL) {
        set_last_error(LOGGING_ERROR_INVALID_PARAMETER);
        return LOGGING_ERROR_INVALID_PARAMETER;
    }

    if (g_logging_state.is_initialized == 0) {
        set_last_error(LOGGING_ERROR_NOT_INITIALIZED);
        call_native_logging_function(LOG_ERROR, "Logging", "Logging not initialized");
        return LOGGING_ERROR_NOT_INITIALIZED;
    }

    pthread_mutex_lock(&g_logging_state.lock);

    // Check if already exists (same func and context)
    custom_output_node_t* current = g_logging_state.custom_outputs;
    while (current != NULL) {
        if (current->func == func && current->context == context) {
            set_last_error(LOGGING_ERROR_CALLBACK_ALREADY_EXISTS);
            pthread_mutex_unlock(&g_logging_state.lock);
            jni_log_printf(JNI_LOG_DEBUG, g_logging_state.log_tag,
                           "Custom logging output already added");
            return 0; // Already added
        }
        current = current->next;
    }

    // Create new node
    custom_output_node_t* new_node = (custom_output_node_t*)malloc(sizeof(custom_output_node_t));
    if (new_node == NULL) {
        set_last_error(LOGGING_ERROR_MEMORY_ALLOCATION);
        pthread_mutex_unlock(&g_logging_state.lock);
        return LOGGING_ERROR_MEMORY_ALLOCATION;
    }

    new_node->func = func;
    new_node->context = context;
    new_node->next = g_logging_state.custom_outputs;
    g_logging_state.custom_outputs = new_node;
    g_logging_state.custom_output_count++;

    jni_log_printf(JNI_LOG_DEBUG, g_logging_state.log_tag,
                   "Added custom logging output, total count: %d, with context %p",
                   g_logging_state.custom_output_count, context);

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
        set_last_error(LOGGING_ERROR_INVALID_PARAMETER);
        return LOGGING_ERROR_INVALID_PARAMETER;
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

            // Do NOT stop the logging thread when the last callback is removed.
            // The Ruby VM thread may still be writing to ruby_stdout/stderr/vmlogger
            // pipes. The logging thread keeps draining those streams harmlessly.
            // Actual pipe teardown happens in logging_shutdown() during ruby_vm_destroy().
            //
            // However, we MUST restore native stdout/stderr to the original terminal
            // FDs. Otherwise the logging thread reads the host process's output from
            // the pipe and silently discards it (no callbacks to dispatch to), which
            // swallows test framework output, Gradle protocol messages, etc.
            if (g_logging_state.custom_output_count == 0) {
                // Flush stdio buffers while stdout/stderr still point to the
                // logging pipe. Any buffered data gets pushed into the pipe so
                // the logging thread can drain it before we restore the FDs.
                fflush(stdout);
                fflush(stderr);

                // Restore native stdout/stderr so host process output bypasses
                // the logging pipe and goes directly to the terminal.
                if (g_logging_state.original_stdout_fd != -1) {
                    dup2(g_logging_state.original_stdout_fd, STDOUT_FILENO);
                }
                if (g_logging_state.original_stderr_fd != -1) {
                    dup2(g_logging_state.original_stderr_fd, STDERR_FILENO);
                }

                // Close the pipe write-ends so the logging thread sees EOF on
                // native stdout/stderr streams. It continues draining Ruby-specific
                // pipes (ruby_stdout, ruby_stderr, vmlogger) which use separate FDs.
                if (g_logging_state.pipe_stdout_write_fd != -1) {
                    close(g_logging_state.pipe_stdout_write_fd);
                    g_logging_state.pipe_stdout_write_fd = -1;
                }
                if (g_logging_state.pipe_stderr_write_fd != -1) {
                    close(g_logging_state.pipe_stderr_write_fd);
                    g_logging_state.pipe_stderr_write_fd = -1;
                }
            }

            pthread_mutex_unlock(&g_logging_state.lock);
            return 0;
        }
        prev = current;
        current = current->next;
    }

    set_last_error(LOGGING_ERROR_CALLBACK_NOT_FOUND);
    pthread_mutex_unlock(&g_logging_state.lock);
    return LOGGING_ERROR_CALLBACK_NOT_FOUND;
}

/* ---- Per-interpreter listener registry implementation ----------------- */

int logging_register_interpreter_listener(int interpreter_id, LogListener listener) {
    if (interpreter_id <= LOG_NATIVE_INTERPRETER_ID) {
        set_last_error(LOGGING_ERROR_INVALID_PARAMETER);
        return LOGGING_ERROR_INVALID_PARAMETER;
    }

    pthread_mutex_lock(&g_logging_state.lock);

    /* If an entry already exists for this id, replace its listener in
     * place (idempotent re-register from the same interpreter is normal
     * — e.g. ruby_interpreter_create may be called after a previous
     * destroy left a stale-but-soon-to-be-overwritten slot). Walking the
     * list keeps insertion order stable, which preserves the "head =
     * oldest = fallback" property the dispatcher relies on. */
    interpreter_listener_node_t* node = g_logging_state.interpreter_listeners;
    while (node != NULL) {
        if (node->interpreter_id == interpreter_id) {
            node->listener = listener;
            pthread_mutex_unlock(&g_logging_state.lock);
            return 0;
        }
        node = node->next;
    }

    interpreter_listener_node_t* new_node = malloc(sizeof(*new_node));
    if (new_node == NULL) {
        set_last_error(LOGGING_ERROR_MEMORY_ALLOCATION);
        pthread_mutex_unlock(&g_logging_state.lock);
        return LOGGING_ERROR_MEMORY_ALLOCATION;
    }
    new_node->interpreter_id = interpreter_id;
    new_node->listener       = listener;
    new_node->next           = NULL;

    /* Append at tail so the head stays the first-ever-registered listener
     * (the natural fallback sink for native logs). */
    if (g_logging_state.interpreter_listeners == NULL) {
        g_logging_state.interpreter_listeners = new_node;
    } else {
        interpreter_listener_node_t* tail = g_logging_state.interpreter_listeners;
        while (tail->next != NULL) tail = tail->next;
        tail->next = new_node;
    }
    g_logging_state.interpreter_listener_count++;

    /* Bring up the dispatch thread on the first interpreter registration
     * (mirrors what logging_add_custom_output does on its first add).
     * Without this, a consumer that ONLY uses the registry path — with
     * no legacy custom_output callbacks — would sit on a never-drained
     * pipe forever. internal_start_logging_thread is idempotent so this
     * is harmless on subsequent registrations. */
    int thread_start_rc = 0;
    if (g_logging_state.interpreter_listener_count == 1 &&
        g_logging_state.custom_output_count == 0 &&
        !g_logging_state.is_running) {
        thread_start_rc = internal_start_logging_thread();
        if (thread_start_rc != 0) {
            /* Roll back the registration: the consumer would otherwise
             * hold a registry entry that never receives lines. */
            interpreter_listener_node_t** link = &g_logging_state.interpreter_listeners;
            while (*link != NULL && *link != new_node) link = &(*link)->next;
            if (*link == new_node) *link = new_node->next;
            free(new_node);
            g_logging_state.interpreter_listener_count--;
        }
    }

    pthread_mutex_unlock(&g_logging_state.lock);
    return thread_start_rc;
}

int logging_unregister_interpreter_listener(int interpreter_id) {
    if (interpreter_id <= LOG_NATIVE_INTERPRETER_ID) {
        set_last_error(LOGGING_ERROR_INVALID_PARAMETER);
        return LOGGING_ERROR_INVALID_PARAMETER;
    }

    pthread_mutex_lock(&g_logging_state.lock);

    interpreter_listener_node_t** link = &g_logging_state.interpreter_listeners;
    while (*link != NULL) {
        if ((*link)->interpreter_id == interpreter_id) {
            interpreter_listener_node_t* to_free = *link;
            *link = to_free->next;
            free(to_free);
            g_logging_state.interpreter_listener_count--;
            pthread_mutex_unlock(&g_logging_state.lock);
            return 0;
        }
        link = &(*link)->next;
    }

    /* Not found: treat as success — caller has nothing more to clean up,
     * and re-entrant unregister (e.g. destroy called twice) is normal. */
    pthread_mutex_unlock(&g_logging_state.lock);
    return 0;
}

/**
 * Get a file descriptor for a specific log stream
 * This allows external code (like Ruby VM) to write directly to a specific log stream
 */
int logging_get_stream_fd(log_stream_t stream) {
    pthread_mutex_lock(&g_logging_state.lock);

    if (!g_logging_state.is_initialized) {
        set_last_error(LOGGING_ERROR_NOT_INITIALIZED);
        pthread_mutex_unlock(&g_logging_state.lock);
        return LOGGING_ERROR_NOT_INITIALIZED;
    }

    if (!g_logging_state.is_running) {
        set_last_error(LOGGING_ERROR_NOT_RUNNING);
        pthread_mutex_unlock(&g_logging_state.lock);
        return LOGGING_ERROR_NOT_RUNNING;
    }

    int stream_index;
    switch (stream) {
        case LOG_STREAM_RUBY_STDOUT:
            stream_index = RUBY_STDOUT_INDEX;
            break;
        case LOG_STREAM_RUBY_STDERR:
            stream_index = RUBY_STDERR_INDEX;
            break;
        case LOG_STREAM_VMLOGGER:
            stream_index = VMLOGGER_INDEX;
            break;
        case LOG_STREAM_NATIVE_STDOUT:
        case LOG_STREAM_NATIVE_STDERR:
            // Native streams are redirected via dup2, don't expose their FDs
            set_last_error(LOGGING_ERROR_INVALID_PARAMETER);
            pthread_mutex_unlock(&g_logging_state.lock);
            return LOGGING_ERROR_INVALID_PARAMETER;
        default:
            set_last_error(LOGGING_ERROR_INVALID_PARAMETER);
            pthread_mutex_unlock(&g_logging_state.lock);
            return LOGGING_ERROR_INVALID_PARAMETER;
    }

    // Return the write end of the socketpair (index 1)
    int fd = g_logging_state.stream_pfd[stream_index][1];

    pthread_mutex_unlock(&g_logging_state.lock);

    if (fd == -1) {
        set_last_error(LOGGING_ERROR_NOT_RUNNING);
        return LOGGING_ERROR_NOT_RUNNING;
    }

    return fd;
}

int logging_get_original_stdout_fd(void) {
    return g_logging_state.original_stdout_fd;
}

int logging_get_original_stderr_fd(void) {
    return g_logging_state.original_stderr_fd;
}

/* Write `len` bytes to `fd` exactly, retrying on partial writes / EINTR.
 * On other errors returns -1; on success returns `len`. EAGAIN is treated
 * as a transient retry too — the saved original-terminal fds are NOT set
 * non-blocking, but a downstream consumer (e.g. unit test) might dup() and
 * change flags, so we cover the case defensively. */
static ssize_t write_all(int fd, const char* buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t n = write(fd, buf + total, len - total);
        if (n > 0) {
            total += (size_t)n;
            continue;
        }
        if (n < 0 && (errno == EINTR || errno == EAGAIN)) {
            continue;
        }
        return -1;
    }
    return (ssize_t)total;
}

ssize_t logging_emit_to_terminal(const char* buf, size_t len) {
    if (buf == NULL || len == 0) return 0;
    int fd = g_logging_state.original_stdout_fd;
    if (fd < 0) return -1;
    return write_all(fd, buf, len);
}

ssize_t logging_emit_to_terminal_err(const char* buf, size_t len) {
    if (buf == NULL || len == 0) return 0;
    int fd = g_logging_state.original_stderr_fd;
    if (fd < 0) return -1;
    return write_all(fd, buf, len);
}
