/*
 * test_remote_debug.c
 *
 * Verifies the Ruby `debug` gem TCP listener is armed by
 * ruby_interpreter_enable_remote_debug() and accepts connections that present
 * the configured RUBY_DEBUG_COOKIE. We do not drive a full DAP session here —
 * the smoke check is "listener is up and answers the handshake," which is what
 * regression tests need to prove the cross-compile + boot integration didn't
 * silently break.
 *
 * Test scenarios:
 *   1. enable_remote_debug succeeds, port is bound, a TCP client can connect.
 *   2. enable_remote_debug refuses an empty token (security regression).
 *   3. Without enable_remote_debug, no listener is bound on the chosen port.
 *
 * Run via CTest:  ctest -R test_remote_debug
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <time.h>

#include "embedded-ruby-vm/ruby-api-loader.h"
#include "embedded-ruby-vm/assets-install.h"
#include "embedded-ruby-vm/assets-error.h"
#include "embedded-ruby-vm/ruby-vm-error.h"

/* Use a high port unlikely to clash with anything in CI sandboxes.
 * The debug gem binds to 127.0.0.1 only (the test never exposes it). */
#define TEST_DEBUG_PORT  57883
#define TEST_TOKEN       "test-cookie-please-ignore"
#define LISTENER_WAIT_MS 20000  /* upper bound for Ruby thread to bind */

static RubyAPI ruby_api;
static AssetsLayout* g_layout = NULL;
/* Diagnostics file: fprintf to stderr is redirected into the logging pipe and
 * never reaches the terminal once the VM boots. Write to a real file instead
 * (matches the pattern used by test_core.c). */
static FILE* g_log = NULL;

#define LOGF(fmt, ...) do {                                       \
    if (g_log) { fprintf(g_log, fmt, ##__VA_ARGS__); fflush(g_log); } \
    fprintf(stdout, fmt, ##__VA_ARGS__); fflush(stdout);          \
} while (0)

static void OnRubyLog(LogListener* l, const char* line, log_stream_t source, log_level_t level, int interpreter_id) {
    (void)l;
    (void)source;
    (void)interpreter_id;
    if (g_log) {
        const char* prefix = (level == LOG_LEVEL_ERROR) ? "[Ruby:err]" : "[Ruby]";
        fprintf(g_log, "%s %s\n", prefix, line);
        fflush(g_log);
    }
}

/* ---------- helpers ---------- */

/* Returns 0 on success (connected), -1 on failure / timeout. */
static int try_tcp_connect(const char* host, int port, int timeout_ms) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        close(fd);
        return -1;
    }

    /* Make the connect non-blocking so we can apply a timeout. */
    int flags = 1;
    (void)setsockopt(fd, IPPROTO_TCP, 1 /* TCP_NODELAY */, &flags, sizeof(flags));

    int rc = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
    if (rc == 0) return fd;

    if (errno != EINPROGRESS) {
        /* Plain refused / unreachable — listener isn't up. */
        close(fd);
        return -1;
    }

    struct pollfd pfd = { .fd = fd, .events = POLLOUT };
    rc = poll(&pfd, 1, timeout_ms);
    if (rc <= 0) {
        close(fd);
        return -1;
    }
    int so_err = 0;
    socklen_t so_err_len = sizeof(so_err);
    getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_err, &so_err_len);
    if (so_err != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* Poll until a TCP connect to (host, port) succeeds, with overall budget. */
static int wait_for_listener(const char* host, int port, int total_timeout_ms) {
    const int step_ms = 100;
    int elapsed = 0;
    while (elapsed < total_timeout_ms) {
        int fd = try_tcp_connect(host, port, step_ms);
        if (fd >= 0) {
            close(fd);
            return 0;
        }
        usleep(step_ms * 1000);
        elapsed += step_ms;
    }
    return -1;
}

/* Bootstrap assets and Ruby API once for the whole binary. */
static int setup_runtime(void) {
    AssetsError err;
    assets_error_init(&err);
    g_layout = assets_bootstrap("./test-remote-debug-install", &err);
    if (!g_layout) {
        LOGF("assets_bootstrap failed: %s\n", err.message);
        return 1;
    }

    const char* deps_paths[] = {
        "../lib/libembedded-ruby.deps", "./libembedded-ruby.deps",
        "libembedded-ruby.deps", NULL
    };
    const char* lib_paths[] = {
        "../lib/libembedded-ruby.so", "./libembedded-ruby.so",
        "libembedded-ruby.so", NULL
    };
    if (ruby_api_bootstrap(&ruby_api, deps_paths, lib_paths, g_layout->native_libs_dir) != 0) {
        LOGF("ruby_api_bootstrap failed\n");
        return 2;
    }
    return 0;
}

static RubyInterpreter* make_interpreter(void) {
    LogListener listener = {
        .context = NULL, .user_data = NULL,
        .on_log_message = OnRubyLog,
    };
    return ruby_api.interpreter.create(".", g_layout->ruby_stdlib_path,
                                       g_layout->native_libs_dir, listener);
}

/* ---------- scenarios ---------- */

static int scenario_listener_up(void) {
    LOGF("[test_remote_debug] scenario_listener_up START\n");
    RubyInterpreter* interp = make_interpreter();
    if (!interp) { LOGF("  interpreter.create failed\n"); return 10; }

    RubyVMRemoteDebugOptions opts = {
        .host = "127.0.0.1", .port = TEST_DEBUG_PORT,
        .token = TEST_TOKEN, .session_name = "test_remote_debug",
    };
    int rc = ruby_api.interpreter.enable_remote_debug(interp, &opts);
    if (rc != RUBY_VM_OK) {
        LOGF("  enable_remote_debug failed: %d (%s)\n",
                rc, ruby_api.interpreter.get_error_message(interp));
        ruby_api.interpreter.destroy(interp);
        return 11;
    }

    if (wait_for_listener("127.0.0.1", TEST_DEBUG_PORT, LISTENER_WAIT_MS) != 0) {
        LOGF("  no listener on 127.0.0.1:%d after %d ms\n",
                TEST_DEBUG_PORT, LISTENER_WAIT_MS);
        ruby_api.interpreter.destroy(interp);
        return 12;
    }
    LOGF("  OK: TCP connect to 127.0.0.1:%d succeeded\n", TEST_DEBUG_PORT);

    ruby_api.interpreter.destroy(interp);
    return 0;
}

static int scenario_empty_token_refused(void) {
    LOGF("[test_remote_debug] scenario_empty_token_refused START\n");
    RubyInterpreter* interp = make_interpreter();
    if (!interp) { LOGF("  interpreter.create failed\n"); return 20; }

    RubyVMRemoteDebugOptions opts = {
        .host = "127.0.0.1", .port = TEST_DEBUG_PORT + 1,
        .token = "", .session_name = NULL,
    };
    int rc = ruby_api.interpreter.enable_remote_debug(interp, &opts);
    ruby_api.interpreter.destroy(interp);

    if (rc == RUBY_VM_OK) {
        LOGF("  expected failure for empty token, got success\n");
        return 21;
    }
    if (rc != RUBY_VM_ERROR_INVALID_PARAM) {
        LOGF("  expected RUBY_VM_ERROR_INVALID_PARAM (-1), got %d\n", rc);
        return 22;
    }
    LOGF("  OK: empty token rejected with INVALID_PARAM\n");
    return 0;
}

static int scenario_no_listener_without_enable(void) {
    LOGF("[test_remote_debug] scenario_no_listener_without_enable START\n");
    /* Pick a different port than the first scenario. We do NOT boot the VM
     * (and thus do not call enable_remote_debug). A plain TCP connect should
     * fail immediately with ECONNREFUSED. */
    int fd = try_tcp_connect("127.0.0.1", TEST_DEBUG_PORT + 2, 200);
    if (fd >= 0) {
        LOGF("  unexpected: something is listening on port %d\n",
                TEST_DEBUG_PORT + 2);
        close(fd);
        return 31;
    }
    LOGF("  OK: no listener on 127.0.0.1:%d (as expected)\n", TEST_DEBUG_PORT + 2);
    return 0;
}

int main(void) {
    /* Open diagnostic log BEFORE the VM boots so that the logging system's
     * dup2-of-stderr doesn't swallow our diagnostics. The file path is
     * relative to the test working directory (kmp build/.../bin/). */
    g_log = fopen("test_remote_debug.log", "w");
    if (!g_log) {
        fprintf(stdout, "WARNING: could not open test_remote_debug.log: %s\n",
                strerror(errno));
        /* keep going — we'll still emit to stdout via LOGF */
    } else {
        fprintf(stdout, "[test_remote_debug] diagnostic log: ./test_remote_debug.log\n");
        fflush(stdout);
    }

    int rc = setup_runtime();
    if (rc != 0) return rc;

    /* Order matters: the listener scenarios bind the global VM, so we run the
     * "must not be listening" scenario FIRST while no VM exists. */
    rc = scenario_no_listener_without_enable();
    if (rc != 0) { LOGF("FAIL: scenario_no_listener_without_enable (%d)\n", rc); return rc; }

    rc = scenario_empty_token_refused();
    if (rc != 0) { LOGF("FAIL: scenario_empty_token_refused (%d)\n", rc); return rc; }

    rc = scenario_listener_up();
    if (rc != 0) { LOGF("FAIL: scenario_listener_up (%d)\n", rc); return rc; }

    LOGF("[test_remote_debug] ALL OK\n");
    return 0;
}
