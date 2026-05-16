#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <poll.h>
#include <stdatomic.h>
#include "embedded-ruby-vm/constants.h"

#include "embedded-ruby-vm/logging.h"
#include "embedded-ruby-vm/ruby-script-location.h"
#include "embedded-ruby-vm/ruby-script.h"
#include "embedded-ruby-vm/ruby-vm.h"
#include "embedded-ruby-vm/exec-main-vm.h"
#include "embedded-ruby-vm/debug.h"

/**
 * Helper structure for thread communication
 */
typedef struct {
    RubyVM* vm;
    char* ruby_base_directory;
    char* native_libs_location;
} RubyVMStartArgs;

/**
 * Helper structure for script execution thread
 */
typedef struct {
    RubyVM* vm;
    int interpreter_id;
    RubyScript* script;
    RubyCompletionTask on_complete;
} ScriptExecutionArgs;

/**
 * Per-script waiter. One is allocated and registered on the VM's FIFO list
 * before bytes hit the wire; the reader thread pops it on the matching
 * response and signals the waiting submitter thread.
 *
 * The list is shared across all interpreter_ids. FIFO order on the list
 * mirrors FIFO order on the wire, which mirrors FIFO order in the
 * dispatcher's per-id worker queue. So "first waiter with matching id" is
 * always the right one.
 */
struct result_waiter {
    int interpreter_id;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int exit_code;
    int completed;            // 0 pending, 1 ready, -1 cancelled by shutdown
    struct result_waiter* next;
};


/**
 * Main thread function for the Ruby VM
 *
 * @param arg Pointer to the Ruby VM instance
 * @return NULL
 */
static void* main_thread_func(void* arg) {
    RubyVMStartArgs* args = (RubyVMStartArgs*)arg;
    RubyVM* vm = args->vm;

    const int exitCode = ExecMainRubyVM(vm,
        args->ruby_base_directory,
        args->native_libs_location
    );

    if (exitCode != 0) {
        fprintf(stderr, "Error during VM execution: %d", exitCode);
    }

    free(args->native_libs_location);
    free(args->ruby_base_directory);
    free(args);
    return NULL;
}

/**
 * Write all bytes to a file descriptor, retrying on partial writes and EINTR.
 *
 * @param fd File descriptor
 * @param buf Data to write
 * @param count Number of bytes to write
 * @return 0 on success, -1 on error
 */
static int write_all(int fd, const void* buf, size_t count) {
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

/**
 * Send a script request to the Ruby VM.
 *
 * Wire format (each frame, request side):
 *     <interpreter_id>\n
 *     <length>\n
 *     <script_content>          (exactly <length> bytes)
 *
 * Caller must hold vm->send_lock so writes from concurrent submitters
 * stay non-interleaved AND match the order in which waiters were
 * registered on the FIFO list.
 *
 * @param socket_fd Socket file descriptor
 * @param interpreter_id Owning interpreter's id
 * @param script_content Script content to send
 * @return 0 on success, negative on error
 */
static int send_script_to_ruby(int socket_fd, int interpreter_id, const char* script_content) {
    size_t script_length = strlen(script_content);
    char header[64];

    int written = snprintf(header, sizeof(header), "%d\n%zu\n", interpreter_id, script_length);
    if (written < 0 || written >= (int)sizeof(header)) {
        fprintf(stderr, "Failed to format script header\n");
        return -1;
    }
    if (write_all(socket_fd, header, (size_t)written) != 0) {
        perror("Failed to write script header");
        return -1;
    }

    if (write_all(socket_fd, script_content, script_length) != 0) {
        perror("Failed to write script content");
        return -1;
    }
    return 0;
}

/**
 * Read one newline-terminated line into buf (no trailing newline; null-
 * terminated). Returns 0 on success, -1 on EOF or error.
 *
 * Used only by the per-VM reader thread, so no mutex required on the fd.
 */
static int read_line(int fd, char* buf, size_t buf_size) {
    size_t pos = 0;
    while (pos + 1 < buf_size) {
        ssize_t n = read(fd, &buf[pos], 1);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) {
            return -1;   // EOF: command channel closed
        }
        if (buf[pos] == '\n') {
            buf[pos] = '\0';
            return 0;
        }
        pos++;
    }
    /* Line too long; consume rest until newline to keep the stream sane. */
    char drop;
    while (1) {
        ssize_t n = read(fd, &drop, 1);
        if (n <= 0) return -1;
        if (drop == '\n') break;
    }
    fprintf(stderr, "protocol error: response line truncated\n");
    return -1;
}

/**
 * Append a waiter to the VM's FIFO list. Caller must hold vm->send_lock
 * so the append happens in the same critical section as the wire write —
 * that's what guarantees registration order matches request order on the
 * wire.
 */
static void waiter_register_locked(RubyVM* vm, result_waiter_t* w) {
    pthread_mutex_lock(&vm->waiters_lock);
    w->next = NULL;
    if (vm->waiters_tail == NULL) {
        vm->waiters_head = w;
        vm->waiters_tail = w;
    } else {
        vm->waiters_tail->next = w;
        vm->waiters_tail = w;
    }
    pthread_mutex_unlock(&vm->waiters_lock);
}

/**
 * Pop the first waiter with the given interpreter_id from the FIFO list.
 * Returns NULL if none found. The reader thread calls this on every
 * response; the submitter waits on the returned waiter's condvar.
 */
static result_waiter_t* waiter_pop_first_for_id(RubyVM* vm, int interpreter_id) {
    pthread_mutex_lock(&vm->waiters_lock);
    result_waiter_t* prev = NULL;
    result_waiter_t* cur = vm->waiters_head;
    while (cur != NULL && cur->interpreter_id != interpreter_id) {
        prev = cur;
        cur = cur->next;
    }
    if (cur != NULL) {
        if (prev == NULL) {
            vm->waiters_head = cur->next;
        } else {
            prev->next = cur->next;
        }
        if (vm->waiters_tail == cur) {
            vm->waiters_tail = prev;
        }
        cur->next = NULL;
    }
    pthread_mutex_unlock(&vm->waiters_lock);
    return cur;
}

/**
 * Mark every still-pending waiter as cancelled. Called at shutdown after
 * the wire is closed and the reader thread has joined — gives any
 * submitters still parked in waiter_wait() a chance to wake up and return
 * with an error rather than hanging the process. */
static void cancel_all_waiters(RubyVM* vm) {
    pthread_mutex_lock(&vm->waiters_lock);
    result_waiter_t* w = vm->waiters_head;
    while (w != NULL) {
        result_waiter_t* next = w->next;
        pthread_mutex_lock(&w->lock);
        w->exit_code = 1;
        w->completed = -1;
        pthread_cond_signal(&w->cond);
        pthread_mutex_unlock(&w->lock);
        w = next;
    }
    vm->waiters_head = NULL;
    vm->waiters_tail = NULL;
    pthread_mutex_unlock(&vm->waiters_lock);
}

/**
 * Block until this waiter is completed (or cancelled). Returns the
 * resolved exit code. The waiter is freed by the caller after this
 * function returns — by that point the reader thread has fully released
 * it (see signal sequence in reader_thread_func / cancel_all_waiters).
 */
static int waiter_wait_and_destroy(result_waiter_t* w) {
    pthread_mutex_lock(&w->lock);
    while (w->completed == 0) {
        pthread_cond_wait(&w->cond, &w->lock);
    }
    int code = w->exit_code;
    pthread_mutex_unlock(&w->lock);

    pthread_cond_destroy(&w->cond);
    pthread_mutex_destroy(&w->lock);
    free(w);
    return code;
}

/**
 * Reader thread body. Reads response frames `<id>\n<exit_code>\n` from the
 * command channel and signals the matching waiter. Exits cleanly on EOF
 * (which is what ruby_vm_destroy provokes by closing the channel).
 */
static void* reader_thread_func(void* arg) {
    RubyVM* vm = (RubyVM*) arg;

    /* Block all signals on this thread too — same reasoning as
     * script_execution_thread_func: Ruby's GC may target a random pthread
     * with SIGPROF/SIGALRM and we don't want this background reader
     * sucking those up. */
    sigset_t set;
    sigfillset(&set);
    pthread_sigmask(SIG_BLOCK, &set, NULL);

    int fd = vm->commands_channel.main_fd;
    char id_buf[32];
    char code_buf[16];

    while (1) {
        if (read_line(fd, id_buf, sizeof(id_buf)) != 0) break;
        if (read_line(fd, code_buf, sizeof(code_buf)) != 0) break;

        int interpreter_id = atoi(id_buf);
        int exit_code = atoi(code_buf);

        result_waiter_t* w = waiter_pop_first_for_id(vm, interpreter_id);
        if (w == NULL) {
            /* Stray response — interpreter destroyed before its result
             * came back, or a protocol mishap. Drop it. */
            DEBUG_LOG("reader: no waiter for interpreter_id=%d, dropping exit_code=%d",
                      interpreter_id, exit_code);
            continue;
        }

        pthread_mutex_lock(&w->lock);
        w->exit_code = exit_code;
        w->completed = 1;
        pthread_cond_signal(&w->cond);
        pthread_mutex_unlock(&w->lock);
    }

    DEBUG_LOG("reader_thread_func: EOF or error on command channel — exiting");
    return NULL;
}

/**
 * Execute a script and return the exit code.
 * This is the core execution logic shared between sync and async execution.
 * Caller is responsible for signal masking if needed.
 *
 * @param vm Ruby VM instance
 * @param script Script to execute
 * @return Exit code (0 = success, non-zero = error)
 */
static int execute_script_internal(RubyVM* vm, int interpreter_id, RubyScript* script) {
    const char* content = ruby_script_get_content(script);

    /* Allocate and pre-init the waiter BEFORE taking send_lock so we don't
     * hold the wire-serialization lock across a malloc. */
    result_waiter_t* w = malloc(sizeof(*w));
    if (w == NULL) {
        fprintf(stderr, "Failed to allocate result waiter\n");
        return 1;
    }
    w->interpreter_id = interpreter_id;
    w->exit_code = 1;
    w->completed = 0;
    w->next = NULL;
    pthread_mutex_init(&w->lock, NULL);
    pthread_cond_init(&w->cond, NULL);

    /* Register + send atomically under send_lock. This pairs each waiter
     * to the request that lands on the wire immediately after — preserves
     * FIFO order between submitters for any given interpreter_id. */
    pthread_mutex_lock(&vm->send_lock);
    waiter_register_locked(vm, w);
    int send_rc = send_script_to_ruby(vm->commands_channel.main_fd, interpreter_id, content);
    pthread_mutex_unlock(&vm->send_lock);

    if (send_rc != 0) {
        fprintf(stderr, "Failed to send script to Ruby VM\n");
        /* The waiter is now orphaned on the list. Cancel it locally so we
         * don't leak — pop by id and free. */
        result_waiter_t* popped = waiter_pop_first_for_id(vm, interpreter_id);
        if (popped == w) {
            pthread_cond_destroy(&w->cond);
            pthread_mutex_destroy(&w->lock);
            free(w);
        } else if (popped != NULL) {
            /* Another submitter snuck in between our register and our
             * cancel pop — push the wrong one back on the head, then try
             * again for ours. Rare; handled defensively. */
            pthread_mutex_lock(&vm->waiters_lock);
            popped->next = vm->waiters_head;
            vm->waiters_head = popped;
            if (vm->waiters_tail == NULL) vm->waiters_tail = popped;
            pthread_mutex_unlock(&vm->waiters_lock);
            /* Fall through to wait — the response will eventually arrive
             * (or shutdown will cancel us). */
            return waiter_wait_and_destroy(w);
        }
        return 1;
    }

    /* Block until the reader thread (or shutdown) signals the waiter. */
    return waiter_wait_and_destroy(w);
}

/**
 * Script execution thread function - executes a single script and terminates
 *
 * @param arg Pointer to ScriptExecutionArgs
 * @return NULL
 */
static void* script_execution_thread_func(void* arg) {
    ScriptExecutionArgs* exec_args = (ScriptExecutionArgs*)arg;
    RubyVM* vm = exec_args->vm;
    int interpreter_id = exec_args->interpreter_id;
    RubyScript* script = exec_args->script;
    RubyCompletionTask on_complete = exec_args->on_complete;

    // Free args immediately since we've copied what we need
    free(exec_args);

    // Block all signals to prevent Ruby's signal handlers from affecting this thread
    // Ruby's GC uses signals to stop threads, but this is a native thread, not a Ruby thread
    sigset_t set;
    sigfillset(&set);
    pthread_sigmask(SIG_BLOCK, &set, NULL);

    DEBUG_LOG("Script execution thread started");

    // Execute the script
    const int result = execute_script_internal(vm, interpreter_id, script);

    DEBUG_LOG("Script execution thread finished - invoking completion callback");

    // Invoke completion callback
    ruby_completion_task_invoke(&on_complete, result);

    // Decrement in-flight count and signal drain if this was the last script
    if (atomic_fetch_sub(&vm->in_flight_scripts, 1) == 1) {
        // We were the last in-flight script — wake up ruby_vm_destroy() if it's waiting
        pthread_mutex_lock(&vm->drain_mutex);
        pthread_cond_signal(&vm->drain_cond);
        pthread_mutex_unlock(&vm->drain_mutex);
    }

    DEBUG_LOG("Script execution thread finished - terminating");
    return NULL;
}

static int native_log_callbacks(const char* line, log_stream_t stream, log_level_t level, void* context) {
    if (context == NULL) {
        return -1;
    }

    RubyVM* vm = (RubyVM*)context;

    if (vm->log_listener.on_log_message == NULL) {
        return -2;
    }
    vm->log_listener.on_log_message(&vm->log_listener, line, stream, level);
    return 0;
}

RubyVM* ruby_vm_create(const char* application_path, RubyScript* main_script, LogListener listener) {
    if (!application_path || !main_script) return NULL;

    RubyVM* vm = malloc(sizeof(RubyVM));
    if (!vm) return NULL;

    vm->application_path = strdup(application_path);
    if (!vm->application_path) {
        free(vm);
        return NULL;
    }
    vm->main_script = main_script;
    vm->log_listener = listener;
    atomic_store(&vm->state, RUBY_VM_STATE_CREATED);
    atomic_store(&vm->in_flight_scripts, 0);
    pthread_mutex_init(&vm->drain_mutex, NULL);
    pthread_cond_init(&vm->drain_cond, NULL);
    pthread_mutex_init(&vm->send_lock, NULL);
    pthread_mutex_init(&vm->waiters_lock, NULL);
    vm->waiters_head = NULL;
    vm->waiters_tail = NULL;
    atomic_store(&vm->reader_thread_started, 0);
    ruby_vm_error_init(&vm->last_error);
    vm->remote_debug = NULL;
    vm->remote_eval = NULL;
    return vm;
}

/* Free a VM-owned RubyVMRemoteDebugOptions. The strings are strdup'd copies
 * (see ruby_vm_enable_remote_debug); we cast away `const` only here, at the
 * point where we know the VM owns them. Public callers should never free
 * their own RubyVMRemoteDebugOptions fields — they pass static or stack-
 * allocated strings. */
static void free_remote_debug(RubyVMRemoteDebugOptions* opts) {
    if (!opts) return;
    free((char*)opts->host);
    free((char*)opts->token);
    free((char*)opts->session_name);
    free(opts);
}

int ruby_vm_enable_remote_debug(RubyVM* vm, const RubyVMRemoteDebugOptions* opts) {
    if (!vm || !opts) {
        return RUBY_VM_ERROR_INVALID_PARAM;
    }

    int expected = RUBY_VM_STATE_CREATED;
    if (atomic_load(&vm->state) != expected) {
        ruby_vm_error_set(&vm->last_error, RUBY_VM_ERROR_ALREADY_STARTED,
                          "ruby_vm_enable_remote_debug must be called before ruby_vm_start "
                          "(current state=%d)", atomic_load(&vm->state));
        return RUBY_VM_ERROR_ALREADY_STARTED;
    }

    if (opts->port <= 0 || opts->port > 65535) {
        ruby_vm_error_set(&vm->last_error, RUBY_VM_ERROR_INVALID_PARAM,
                          "Invalid port for remote debug: %d (must be 1..65535)", opts->port);
        return RUBY_VM_ERROR_INVALID_PARAM;
    }

    if (!opts->token || opts->token[0] == '\0') {
        ruby_vm_error_set(&vm->last_error, RUBY_VM_ERROR_INVALID_PARAM,
                          "Remote debug token is required (must be a non-empty shared secret)");
        return RUBY_VM_ERROR_INVALID_PARAM;
    }

    RubyVMRemoteDebugOptions* owned = calloc(1, sizeof(RubyVMRemoteDebugOptions));
    if (!owned) {
        ruby_vm_error_set(&vm->last_error, RUBY_VM_ERROR_OUT_OF_MEMORY,
                          "Failed to allocate remote debug config");
        return RUBY_VM_ERROR_OUT_OF_MEMORY;
    }

    owned->host = strdup(opts->host ? opts->host : "127.0.0.1");
    owned->port = opts->port;
    owned->token = strdup(opts->token);
    owned->session_name = opts->session_name ? strdup(opts->session_name) : NULL;

    if (!owned->host || !owned->token || (opts->session_name && !owned->session_name)) {
        free_remote_debug(owned);
        ruby_vm_error_set(&vm->last_error, RUBY_VM_ERROR_OUT_OF_MEMORY,
                          "Failed to copy remote debug strings");
        return RUBY_VM_ERROR_OUT_OF_MEMORY;
    }

    free_remote_debug(vm->remote_debug);
    vm->remote_debug = owned;
    return RUBY_VM_OK;
}

/* Free a VM-owned RubyVMRemoteEvalOptions. Sibling of free_remote_debug —
 * same ownership contract. */
static void free_remote_eval(RubyVMRemoteEvalOptions* opts) {
    if (!opts) return;
    free((char*)opts->host);
    free((char*)opts->token);
    free((char*)opts->session_name);
    free(opts);
}

int ruby_vm_enable_remote_eval(RubyVM* vm, const RubyVMRemoteEvalOptions* opts) {
    if (!vm || !opts) {
        return RUBY_VM_ERROR_INVALID_PARAM;
    }

    int expected = RUBY_VM_STATE_CREATED;
    if (atomic_load(&vm->state) != expected) {
        ruby_vm_error_set(&vm->last_error, RUBY_VM_ERROR_ALREADY_STARTED,
                          "ruby_vm_enable_remote_eval must be called before ruby_vm_start "
                          "(current state=%d)", atomic_load(&vm->state));
        return RUBY_VM_ERROR_ALREADY_STARTED;
    }

    if (opts->port <= 0 || opts->port > 65535) {
        ruby_vm_error_set(&vm->last_error, RUBY_VM_ERROR_INVALID_PARAM,
                          "Invalid port for remote eval: %d (must be 1..65535)", opts->port);
        return RUBY_VM_ERROR_INVALID_PARAM;
    }

    if (!opts->token || opts->token[0] == '\0') {
        ruby_vm_error_set(&vm->last_error, RUBY_VM_ERROR_INVALID_PARAM,
                          "Remote eval token is required (must be a non-empty shared secret)");
        return RUBY_VM_ERROR_INVALID_PARAM;
    }

    RubyVMRemoteEvalOptions* owned = calloc(1, sizeof(RubyVMRemoteEvalOptions));
    if (!owned) {
        ruby_vm_error_set(&vm->last_error, RUBY_VM_ERROR_OUT_OF_MEMORY,
                          "Failed to allocate remote eval config");
        return RUBY_VM_ERROR_OUT_OF_MEMORY;
    }

    owned->host = strdup(opts->host ? opts->host : "127.0.0.1");
    owned->port = opts->port;
    owned->token = strdup(opts->token);
    owned->session_name = opts->session_name ? strdup(opts->session_name) : NULL;

    if (!owned->host || !owned->token || (opts->session_name && !owned->session_name)) {
        free_remote_eval(owned);
        ruby_vm_error_set(&vm->last_error, RUBY_VM_ERROR_OUT_OF_MEMORY,
                          "Failed to copy remote eval strings");
        return RUBY_VM_ERROR_OUT_OF_MEMORY;
    }

    free_remote_eval(vm->remote_eval);
    vm->remote_eval = owned;
    return RUBY_VM_OK;
}

void ruby_vm_destroy(RubyVM* vm) {
    if (!vm) return;

    // Transition to SHUTTING_DOWN — reject new enqueues from this point
    int was_running = 0;
    int expected = RUBY_VM_STATE_RUNNING;
    if (atomic_compare_exchange_strong(&vm->state, &expected, RUBY_VM_STATE_SHUTTING_DOWN)) {
        was_running = 1;
    } else {
        // If not RUNNING, it may be CREATED (never started) — try that too
        expected = RUBY_VM_STATE_CREATED;
        if (!atomic_compare_exchange_strong(&vm->state, &expected, RUBY_VM_STATE_SHUTTING_DOWN)) {
            DEBUG_LOG("ruby_vm_destroy: VM already shutting down or destroyed (state=%d)", expected);
            return;
        }
    }

    DEBUG_LOG("ruby_vm_destroy: Draining in-flight scripts");
    // Wait for all in-flight script threads to complete
    pthread_mutex_lock(&vm->drain_mutex);
    while (atomic_load(&vm->in_flight_scripts) > 0) {
        DEBUG_LOG("ruby_vm_destroy: Waiting for %d in-flight script(s)", atomic_load(&vm->in_flight_scripts));
        pthread_cond_wait(&vm->drain_cond, &vm->drain_mutex);
    }
    pthread_mutex_unlock(&vm->drain_mutex);
    DEBUG_LOG("ruby_vm_destroy: All scripts drained");

    if (was_running) {
        DEBUG_LOG("ruby_vm_destroy: Closing command channel to signal FIFO interpreter exit");
        // Close the command channel FIRST — this causes the Ruby FIFO interpreter
        // to see EOF on socket.gets and break out of its loop cleanly.
        // Logging must stay alive during this phase so the Ruby VM thread can
        // still write to stdout/stderr/vmlogger without hitting broken pipes.
        close_comm_channel(&vm->commands_channel);

        /* Closing the channel also EOFs the response side, which is what
         * the reader_thread is parked on inside read_line. Join it so we
         * can safely cancel any leftover waiters next. */
        if (atomic_load(&vm->reader_thread_started)) {
            DEBUG_LOG("ruby_vm_destroy: Joining reader thread");
            pthread_join(vm->reader_thread, NULL);
            atomic_store(&vm->reader_thread_started, 0);
            DEBUG_LOG("ruby_vm_destroy: Reader thread joined");
        }

        /* Anyone parked in execute_script_internal -> waiter_wait_and_destroy
         * is now unreachable from the reader thread (it's gone). Wake them
         * with an error code so they return rather than hang the process. */
        cancel_all_waiters(vm);

        DEBUG_LOG("ruby_vm_destroy: Joining main VM thread");
        // Wait for the Ruby VM thread to fully exit (including Ruby cleanup).
        // Without this join, ruby_run_node() may still be running when we tear
        // down logging pipes and free the VM struct, causing SIGPIPE storms
        // and potentially exit_group() killing the entire process.
        pthread_join(vm->main_thread, NULL);
        DEBUG_LOG("ruby_vm_destroy: Main VM thread joined");
    }

    DEBUG_LOG("ruby_vm_destroy: Disabling logging for VM");
    ruby_vm_disable_logging(vm);

    DEBUG_LOG("ruby_vm_destroy: Shutting down logging system");
    // Full logging teardown: stop the drain thread, close pipes, restore stdout/stderr.
    // This is safe because the Ruby VM thread is no longer running (joined above).
    logging_shutdown();

    // Destroy synchronization primitives
    pthread_mutex_destroy(&vm->send_lock);
    pthread_mutex_destroy(&vm->waiters_lock);
    pthread_cond_destroy(&vm->drain_cond);
    pthread_mutex_destroy(&vm->drain_mutex);

    // Mark as fully destroyed before freeing
    atomic_store(&vm->state, RUBY_VM_STATE_DESTROYED);

    DEBUG_LOG("ruby_vm_destroy: Freeing VM memory");
    free_remote_debug(vm->remote_debug);
    free_remote_eval(vm->remote_eval);
    free(vm->application_path);
    free(vm);
}

/**
 * Shared setup for ruby_vm_start() and ruby_vm_start_on_current_thread().
 * Validates state, enables logging, and creates the communication channel.
 *
 * @param vm Pointer to the Ruby VM instance
 * @return RUBY_VM_OK on success, negative error code on failure
 */
static int ruby_vm_start_setup(RubyVM* vm) {
    if (!vm) {
        return RUBY_VM_ERROR_INVALID_PARAM;
    }

    // Only start from CREATED state
    int expected = RUBY_VM_STATE_CREATED;
    if (!atomic_compare_exchange_strong(&vm->state, &expected, RUBY_VM_STATE_CREATED)) {
        // State was not CREATED — already started or shutting down
        ruby_vm_error_set(&vm->last_error, RUBY_VM_ERROR_ALREADY_STARTED,
                          "VM is already started (state=%d)", expected);
        return RUBY_VM_ERROR_ALREADY_STARTED;
    }

    // Clear any previous errors
    ruby_vm_clear_error(vm);

    // Auto-enable logging if a listener was provided
    // This ensures log streams are available before Ruby VM needs them
    if (vm->log_listener.on_log_message != NULL) {
        DEBUG_LOG("ruby_vm_start_setup: Auto-enabling logging (listener provided)");
        int logging_result = ruby_vm_enable_logging(vm);
        if (logging_result != 0) {
            DEBUG_LOG("ruby_vm_start_setup: Warning - failed to enable logging (error %d)", logging_result);
            // Continue anyway - VM can still work without logging
        }
    }

    DEBUG_LOG("ruby_vm_start_setup: Creating socket pair");
    // Create socket pair for communication
    if (create_comm_channel(&vm->commands_channel) != 0) {
        DEBUG_LOG("ruby_vm_start_setup: Failed to create comm channel");
        ruby_vm_error_set(&vm->last_error, RUBY_VM_ERROR_COMM_CHANNEL,
                          "Failed to create communication channel (socketpair failed)");
        return RUBY_VM_ERROR_COMM_CHANNEL;
    }
    DEBUG_LOG("ruby_vm_start_setup: Socket pair created");

    /* Spawn the response-reader thread. It demuxes `<id>\n<exit_code>\n`
     * frames from the Ruby side and signals matching waiters. Must exist
     * before any script can be submitted, so we spawn it here while the
     * VM is still in CREATED — submitters that race the state flip to
     * RUNNING are gated by the atomic_load check in execute_sync/enqueue
     * anyway. */
    int reader_rc = pthread_create(&vm->reader_thread, NULL, reader_thread_func, vm);
    if (reader_rc != 0) {
        DEBUG_LOG("ruby_vm_start_setup: Failed to spawn reader thread (error %d)", reader_rc);
        close_comm_channel(&vm->commands_channel);
        ruby_vm_error_set(&vm->last_error, RUBY_VM_ERROR_THREAD_CREATE,
                          "Failed to create response-reader thread (error %d)", reader_rc);
        return RUBY_VM_ERROR_THREAD_CREATE;
    }
    atomic_store(&vm->reader_thread_started, 1);
    DEBUG_LOG("ruby_vm_start_setup: Reader thread spawned");

    return RUBY_VM_OK;
}

int ruby_vm_start(RubyVM* vm, const char* ruby_base_directory, const char* native_libs_location) {
    int setup_result = ruby_vm_start_setup(vm);
    if (setup_result != RUBY_VM_OK) return setup_result;

    // Create thread arguments
    DEBUG_LOG("ruby_vm_start: Preparing thread args");
    RubyVMStartArgs* transferredMemoryArgs = malloc(sizeof(RubyVMStartArgs));
    if (!transferredMemoryArgs) {
        ruby_vm_error_set(&vm->last_error, RUBY_VM_ERROR_OUT_OF_MEMORY,
                          "Failed to allocate memory for VM start args");
        return RUBY_VM_ERROR_OUT_OF_MEMORY;
    }
    transferredMemoryArgs->vm = vm;
    transferredMemoryArgs->ruby_base_directory = strdup(ruby_base_directory);
    transferredMemoryArgs->native_libs_location = strdup(native_libs_location);

    if (!transferredMemoryArgs->ruby_base_directory || !transferredMemoryArgs->native_libs_location) {
        ruby_vm_error_set(&vm->last_error, RUBY_VM_ERROR_OUT_OF_MEMORY,
                          "Failed to allocate memory for VM path strings");
        free(transferredMemoryArgs->ruby_base_directory);
        free(transferredMemoryArgs->native_libs_location);
        free(transferredMemoryArgs);
        return RUBY_VM_ERROR_OUT_OF_MEMORY;
    }

    // Start main thread
    // "transferredMemoryArgs" is consumed and freed by the main thread
    DEBUG_LOG("ruby_vm_start: Creating main VM thread");
    int thread_result = pthread_create(&vm->main_thread, NULL, main_thread_func, transferredMemoryArgs);
    if (thread_result != 0) {
        DEBUG_LOG("ruby_vm_start: Failed to create main VM thread");
        ruby_vm_error_set(&vm->last_error, RUBY_VM_ERROR_THREAD_CREATE,
                          "Failed to create Ruby VM thread (error code: %d)", thread_result);
        free(transferredMemoryArgs->ruby_base_directory);
        free(transferredMemoryArgs->native_libs_location);
        free(transferredMemoryArgs);
        return RUBY_VM_ERROR_THREAD_CREATE;
    }
    DEBUG_LOG("ruby_vm_start: Main VM thread created");

    atomic_store(&vm->state, RUBY_VM_STATE_RUNNING);
    DEBUG_LOG("ruby_vm_start: VM started successfully, returning");
    return RUBY_VM_OK;
}

int ruby_vm_start_on_current_thread(RubyVM* vm, const char* ruby_base_directory,
                                     const char* native_libs_location) {
    int setup_result = ruby_vm_start_setup(vm);
    if (setup_result != RUBY_VM_OK) return setup_result;

    atomic_store(&vm->state, RUBY_VM_STATE_RUNNING);
    DEBUG_LOG("ruby_vm_start_on_current_thread: VM running, executing FIFO interpreter inline");

    // Run the FIFO interpreter inline — this BLOCKS until the socket is closed
    const int exitCode = ExecMainRubyVM(vm, ruby_base_directory, native_libs_location);

    if (exitCode != 0) {
        fprintf(stderr, "Error during inline VM execution: %d", exitCode);
    }

    DEBUG_LOG("ruby_vm_start_on_current_thread: FIFO interpreter exited with code %d", exitCode);
    return exitCode;
}

int ruby_vm_enable_logging(RubyVM* vm) {

    // Setup log reading callbacks (but don't start logging thread yet)
    DEBUG_LOG("ruby_vm_enable_logging: Setting up logging callbacks");
    
    // TODO add a way to override the tag
    const int init_result = logging_init("com.scorbutics.rubyvm");
    const int logging_result = init_result != 0 ? init_result : logging_add_custom_output(native_log_callbacks, vm);

    if (logging_result != 0) {
        DEBUG_LOG("ruby_vm_enable_logging: Logging thread failed to start (error %d during %s)", logging_result, init_result != 0 ? "initialization": "adding custom output");
        DEBUG_LOG("Continuing without logging redirection - output will go to normal stdout/stderr");
        ruby_vm_error_set(&vm->last_error, RUBY_VM_ERROR_LOGGING,
                          "Failed to start logging thread (error code: %d)", logging_result);
        return logging_result;
    }

    DEBUG_LOG("ruby_vm_enable_logging: Logging thread started successfully");
    return 0;
}

int ruby_vm_disable_logging(RubyVM* vm) {
    DEBUG_LOG("ruby_vm_disable_logging: Removing logging callback for this VM");

    // CRITICAL FIX: We should remove only THIS VM's callback, not shutdown everything!
    // logging_shutdown() would shut down the entire logging system for ALL VMs.
    // Instead, we remove just this VM's custom output callback.
    // The logging thread will automatically stop when the last callback is removed.
    const int result = logging_remove_custom_output(native_log_callbacks, vm);

    if (result != 0) {
        DEBUG_LOG("ruby_vm_disable_logging: Failed to remove logging callback (error %d)", result);
        // Note: -1 means callback was not found, which could mean it was never added
        // or already removed. This is not necessarily a fatal error.
        if (result == -1) {
            DEBUG_LOG("ruby_vm_disable_logging: Callback not found (may not have been enabled)");
            return 0; // Not an error - logging may not have been enabled
        }
        ruby_vm_error_set(&vm->last_error, RUBY_VM_ERROR_LOGGING,
                          "Failed to remove logging callback (error code: %d)", result);
        return result;
    }

    DEBUG_LOG("ruby_vm_disable_logging: Logging callback removed successfully");
    return 0;
}

void ruby_vm_enqueue(RubyVM* vm, int interpreter_id, RubyScript* script, RubyCompletionTask on_complete) {
    if (!vm || !script) {
        DEBUG_LOG("ruby_vm_enqueue: Invalid parameters");
        ruby_completion_task_invoke(&on_complete, 1);
        return;
    }

    // Check VM state and atomically increment in-flight count
    // We must increment BEFORE checking state to avoid a race with destroy
    atomic_fetch_add(&vm->in_flight_scripts, 1);

    if (atomic_load(&vm->state) != RUBY_VM_STATE_RUNNING) {
        DEBUG_LOG("ruby_vm_enqueue: VM is not running (state=%d), rejecting script", atomic_load(&vm->state));
        atomic_fetch_sub(&vm->in_flight_scripts, 1);
        ruby_completion_task_invoke(&on_complete, 1);
        return;
    }

    // Allocate args for the execution thread
    ScriptExecutionArgs* args = malloc(sizeof(ScriptExecutionArgs));
    if (!args) {
        fprintf(stderr, "Failed to allocate script execution args\n");
        atomic_fetch_sub(&vm->in_flight_scripts, 1);
        ruby_completion_task_invoke(&on_complete, 1);
        return;
    }

    args->vm = vm;
    args->interpreter_id = interpreter_id;
    args->script = script;
    args->on_complete = on_complete;

    // Create a new thread to execute this script
    pthread_t execution_thread;
    int thread_result = pthread_create(&execution_thread, NULL, script_execution_thread_func, args);
    if (thread_result != 0) {
        fprintf(stderr, "Failed to create script execution thread (error code: %d)\n", thread_result);
        free(args);
        atomic_fetch_sub(&vm->in_flight_scripts, 1);
        ruby_completion_task_invoke(&on_complete, 1);
        return;
    }

    // Detach the thread so it cleans up automatically when done
    pthread_detach(execution_thread);

    DEBUG_LOG("ruby_vm_enqueue: Script execution thread created and detached");
}

int ruby_vm_execute_sync(RubyVM* vm, int interpreter_id, RubyScript* script) {
    if (!vm || !script) {
        DEBUG_LOG("ruby_vm_execute_sync: Invalid parameters");
        return 1;
    }

    // Check VM state and increment in-flight count (same pattern as enqueue)
    atomic_fetch_add(&vm->in_flight_scripts, 1);

    if (atomic_load(&vm->state) != RUBY_VM_STATE_RUNNING) {
        DEBUG_LOG("ruby_vm_execute_sync: VM is not running (state=%d)", atomic_load(&vm->state));
        atomic_fetch_sub(&vm->in_flight_scripts, 1);
        return 1;
    }

    DEBUG_LOG("ruby_vm_execute_sync: Executing script synchronously on calling thread");

    // Reset the sentinel before execution so we can wait for a fresh one
    logging_reset_sentinel();

    // Block all signals before entering Ruby execution context
    // This prevents Ruby's GC signals (SIGPROF/SIGALRM) from hitting JVM threads
    // which can cause segmentation faults when Ruby's signal handler tries to
    // interact with JVM-managed memory.
    //
    // Context: Ruby's GC uses signals to coordinate thread pausing during the
    // marking phase. When a JVM thread calls this function and Ruby's GC runs,
    // the signal can be delivered to the JVM thread. Ruby's signal handler may
    // attempt to scan the thread's stack for Ruby objects, but the JVM thread
    // has a JVM-managed stack, not a Ruby thread stack. This mismatch causes
    // a segmentation fault, typically manifesting AFTER the native call returns
    // and the JVM thread continues execution.
    sigset_t new_set, old_set;
    sigfillset(&new_set);
    pthread_sigmask(SIG_BLOCK, &new_set, &old_set);

    // Execute the script using shared internal implementation
    const int result = execute_script_internal(vm, interpreter_id, script);

    // Restore original signal mask before returning to JVM context
    pthread_sigmask(SIG_SETMASK, &old_set, NULL);

    // Wait for all logs to be flushed before returning.
    // The logging thread intercepts the sentinel on LOG_STREAM_VMLOGGER
    // (processed after RUBY_STDOUT and RUBY_STDERR), ensuring all
    // user-visible output has been dispatched to callbacks.
    if (logging_wait_for_sentinel(5000) != 0) {
        DEBUG_LOG("ruby_vm_execute_sync: Timeout waiting for log flush sentinel");
    }

    // Decrement in-flight count and signal drain if this was the last script
    if (atomic_fetch_sub(&vm->in_flight_scripts, 1) == 1) {
        pthread_mutex_lock(&vm->drain_mutex);
        pthread_cond_signal(&vm->drain_cond);
        pthread_mutex_unlock(&vm->drain_mutex);
    }

    DEBUG_LOG("ruby_vm_execute_sync: Script execution completed with result: %d", result);

    return result;
}

const RubyVMError* ruby_vm_get_last_error(const RubyVM* vm) {
    if (!vm) return NULL;
    return &vm->last_error;
}

void ruby_vm_clear_error(RubyVM* vm) {
    if (!vm) return;
    ruby_vm_error_init(&vm->last_error);
}

const char* ruby_vm_get_error_message(const RubyVM* vm) {
    if (!vm) return "Invalid VM pointer";
    if (vm->last_error.code == RUBY_VM_OK) return NULL;

    if (vm->last_error.message[0] != '\0') {
        return vm->last_error.message;
    }
    return ruby_vm_error_string(vm->last_error.code);
}
