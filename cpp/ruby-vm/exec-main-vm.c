
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#include "constants.h"
#include "exec-main-vm.h"
#include "ruby-vm.h"
#include "install.h"

#include "ruby/config.h"
#include "ruby/version.h"

static void SetupRubyEnv(const char* baseDirectory, const char* extraLoadPath)
{
#define RUBY_BUFFER_PATH_SIZE (256)
#define RUBY_NUM_PATH_IN_RUBYLIB_ENV_VAR (3)

#ifndef NDEBUG
    char mess[1024];
#endif
    char rubyVersion[64];
    snprintf(rubyVersion, sizeof(rubyVersion), "%d.%d.%d", RUBY_API_VERSION_MAJOR, RUBY_API_VERSION_MINOR, RUBY_API_VERSION_TEENY);

    const size_t baseDirectorySize = strlen(baseDirectory);
    const size_t maxRubyDirBufferSize = RUBY_NUM_PATH_IN_RUBYLIB_ENV_VAR *
            ((baseDirectorySize * sizeof(char) + sizeof(char)) +
            (strlen(rubyVersion) * sizeof(char)) +
            RUBY_BUFFER_PATH_SIZE);

    char* rubyBufferDir = (char*) malloc(maxRubyDirBufferSize);
    snprintf(rubyBufferDir, maxRubyDirBufferSize, "%s/ruby/gems/%s/", baseDirectory, rubyVersion);
    setenv("GEM_HOME", rubyBufferDir, 1);
    setenv("GEM_PATH", rubyBufferDir, 1);
    strncat(rubyBufferDir, "specifications/", maxRubyDirBufferSize);
    setenv("GEM_SPEC_CACHE", rubyBufferDir, 1);

    snprintf(rubyBufferDir, maxRubyDirBufferSize, "%s:%s/ruby/%s/:%s/ruby/%s/"RUBY_PLATFORM"/:%s", baseDirectory, baseDirectory, rubyVersion, baseDirectory, rubyVersion, extraLoadPath);
    setenv("RUBYLIB", rubyBufferDir, 1);

#ifndef NDEBUG
    snprintf(mess, sizeof(mess), "Ruby VM env. variables :\nGEM_HOME = '%s'\nGEM_PATH = '%s'\nGEM_SPEC_CACHE = '%s'\nRUBYLIB = '%s'",
             getenv("GEM_HOME"), getenv("GEM_PATH"), getenv("GEM_SPEC_CACHE"), getenv("RUBYLIB"));
    printf("%s\n", mess);
#endif

    free(rubyBufferDir);
}

#pragma GCC diagnostic push
#pragma clang diagnostic ignored "-Wdefault-const-init-field-unsafe"
#pragma clang diagnostic ignored "-Wunused-parameter"
#include "ruby/ruby.h"
#pragma GCC diagnostic pop

// Build argv with variable number of arguments
static char** build_ruby_argv_va(int* argc_out,
                                 const char* script_content,
                                 int from_filename,
                                 int extra_args_count,
                                 ...)
{
    va_list args;
    va_start(args, extra_args_count);

    int base_argc = from_filename == 0 ? 3 : 2;
    int argc = base_argc + extra_args_count;

    char **argv = (char**) malloc(sizeof(char*) * argc);
    int idx = 0;

    // Base arguments
    argv[idx++] = strdup("ruby");

    if (from_filename == 0) {
        argv[idx++] = strdup("-e");
    }

    argv[idx++] = strdup(script_content);

    // Extra arguments
    for (int i = 0; i < extra_args_count; i++) {
        const char* arg = va_arg(args, const char*);
        argv[idx++] = strdup(arg);
    }

    va_end(args);

    *argc_out = argc;
    return argv;
}

static void free_ruby_argv(char** argv, int argc) {
    for (int i = 0; i < argc; i++) {
        free(argv[i]);
    }
    free(argv);
}

// Store original signal handlers
static struct sigaction original_sigpipe;
static struct sigaction original_sigchld;
static struct sigaction original_sigsegv;

/**
 * Save original Android signal handlers before Ruby overwrites them
 */
static void SaveOriginalSignalHandlers(void) {
    sigaction(SIGPIPE, NULL, &original_sigpipe);
    sigaction(SIGCHLD, NULL, &original_sigchld);
    sigaction(SIGSEGV, NULL, &original_sigsegv);
}

/**
 * Restore critical signal handlers after Ruby initialization
 */
static void RestoreCriticalSignalHandlers(void) {
    // Restore SIGPIPE - critical for Android's Binder IPC
    if (sigaction(SIGPIPE, &original_sigpipe, NULL) != 0) {
        fprintf(stderr, "Failed to restore SIGPIPE handler\n");
    }

    // For SIGCHLD, we need a compromise - chain handlers
    // (Ruby needs it for Process.wait, Android needs it for Runtime.exec)
}

/**
 * Custom SIGCHLD handler that chains to both Ruby and Android
 */
static void ChainedSigchldHandler(int sig, siginfo_t *info, void *context) {
    // Call original Android handler first
    if (original_sigchld.sa_flags & SA_SIGINFO) {
        if (original_sigchld.sa_sigaction != NULL) {
            original_sigchld.sa_sigaction(sig, info, context);
        }
    } else {
        if (original_sigchld.sa_handler != NULL &&
            original_sigchld.sa_handler != SIG_DFL &&
            original_sigchld.sa_handler != SIG_IGN) {
            original_sigchld.sa_handler(sig);
        }
    }

    // Let Ruby also handle it (it will reap its own child processes)
    // Ruby's handler is now installed - we don't call it directly,
    // just let signal propagate if needed
}

/**
 * Setup compromise signal handling
 */
static void SetupCompromiseSignalHandlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));

    // Setup chained SIGCHLD handler
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = ChainedSigchldHandler;

    if (sigaction(SIGCHLD, &sa, NULL) != 0) {
        fprintf(stderr, "Failed to setup chained SIGCHLD handler\n");
    }
}

static int run_main_vm_node(const char* baseDirectory,
                            const char* rubyExtraLoadPath,
                            const char* scriptContent,
                            int fromFilename,
                            int socket_fd)
{
    SetupRubyEnv(baseDirectory, rubyExtraLoadPath);

    // Step 1: Save Android's original signal handlers
    SaveOriginalSignalHandlers();

    char socket_fd_str[32];
    snprintf(socket_fd_str, sizeof(socket_fd_str), "%d", socket_fd);

    int argc;
    char **argv = build_ruby_argv_va(&argc, scriptContent, fromFilename,
                                     1, socket_fd_str);  // 1 extra arg

    // Step 2: Initialize Ruby (this will overwrite signal handlers)
    ruby_sysinit(&argc, &argv);

    {
        RUBY_INIT_STACK;
        ruby_init();

        // Step 3: Restore critical handlers that Android needs
        RestoreCriticalSignalHandlers();

        // Step 4: Setup compromise handlers for shared signals
        SetupCompromiseSignalHandlers();

        // Step 5: Disable Ruby's signal handling for problematic signals
        // This is done via Ruby's API
        rb_eval_string(
                "Signal.trap('PIPE', 'SYSTEM_DEFAULT')\n"  // Let system handle SIGPIPE
        );

        void* options = ruby_options(argc, argv);
        const int result = ruby_run_node(options);

        free_ruby_argv(argv, argc);
        return result;
    }
}

int ExecMainRubyVM(const char* scriptContent, int commandsFd,
                   const char* rubyDirectoryPath, const char* nativeLibsDirLocation)
{
    if (install_embedded_files(rubyDirectoryPath) != 0) {
        fprintf(stderr, "Error while installing ruby standard files\n");
        return -1;
    }

    printf( "Installation of ruby standard library success!\n");
    return run_main_vm_node(rubyDirectoryPath, nativeLibsDirLocation, scriptContent, 0, commandsFd);
}
