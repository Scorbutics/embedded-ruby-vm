/*
 * Extra ZIPs registered by the host library (assets_register_extra_zip):
 *   - registering one changes the installation version, so an existing
 *     installation is installed again;
 *   - its entries are extracted at the root of the install directory;
 *   - that root is on the Ruby load path: the VM requires the extracted file.
 *
 * EXTRA_ZIP_PATH is a ZIP made by CMake from extra_zip/ (extra_zip_test/hello.rb).
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "embedded-ruby-vm/assets-error.h"
#include "embedded-ruby-vm/assets-install.h"
#include "embedded-ruby-vm/log-listener.h"
#include "embedded-ruby-vm/ruby-api-loader.h"

#ifndef EXTRA_ZIP_PATH
#error "EXTRA_ZIP_PATH must be defined"
#endif

#define INSTALL_DIR "./test-ruby-install-extra-zip"

static RubyAPI g_ruby_api;

/* Print directly to pre-redirect terminal stderr, so diag output survives
 * logging shutdown (same trick as test_same_id_fifo). */
static void diag(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf) - 1, fmt, args);
    va_end(args);
    if (n <= 0) return;
    if (n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;
    buf[n++] = '\n';
    int fd = logging_get_original_stderr_fd();
    if (fd < 0) fd = 2;
    ssize_t ignored = write(fd, buf, (size_t)n);
    (void)ignored;
}

static void on_log(LogListener* listener, const char* line, log_stream_t stream, log_level_t level, int interpreter_id) {
    (void) listener;
    (void) stream;
    (void) level;
    (void) interpreter_id;
    if (strstr(line, "SystemExit") != NULL) return;
    if (strstr(line, "exit intercepted") != NULL) return;
    if (strstr(line, "[SafeRunner]") != NULL) return;
    diag("[ruby] %s", line);
}

static const char* CHECK_SCRIPT =
    "require 'extra_zip_test/hello'\n"
    "exit(10) unless ExtraZipTest.hello == 'hello from an extra zip'\n";

static char* read_file(const char* path, size_t* size) {
    FILE* f = fopen(path, "rb");
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* data = malloc((size_t)length);
    if (data != NULL && fread(data, 1, (size_t)length, f) != (size_t)length) {
        free(data);
        data = NULL;
    }
    fclose(f);
    *size = (size_t)length;
    return data;
}

int main(void) {
    int test_rc = 1;
    AssetsLayout* layout = NULL;
    AssetsError assets_error;
    RubyInterpreter* interp = NULL;
    struct stat st;
    size_t zip_size = 0;
    char* zip = read_file(EXTRA_ZIP_PATH, &zip_size);

    if (zip == NULL) {
        diag("FAIL: cannot read %s", EXTRA_ZIP_PATH);
        return 1;
    }

    assets_error_init(&assets_error);
    if (install_embedded_files(INSTALL_DIR, &assets_error) != 0 || installation_needed(INSTALL_DIR, &assets_error) != 0) {
        diag("FAIL: installation without extra ZIP: %s", assets_error.message);
        goto cleanup;
    }

    if (assets_register_extra_zip("extra-zip-test.zip", zip, zip_size) != ASSETS_OK) {
        diag("FAIL: assets_register_extra_zip");
        goto cleanup;
    }
    if (installation_needed(INSTALL_DIR, &assets_error) != 1) {
        diag("FAIL: registering an extra ZIP must change the installation version");
        goto cleanup;
    }

    layout = assets_bootstrap(INSTALL_DIR, &assets_error);
    if (layout == NULL) {
        diag("FAIL: bootstrap: %s", assets_error.message);
        goto cleanup;
    }
    if (stat(INSTALL_DIR "/extra_zip_test/hello.rb", &st) != 0 || !S_ISREG(st.st_mode)) {
        diag("FAIL: extra_zip_test/hello.rb was not extracted at the install root");
        goto cleanup;
    }
    if (installation_needed(INSTALL_DIR, &assets_error) != 0) {
        diag("FAIL: installation with the extra ZIP is not up to date");
        goto cleanup;
    }

    const char* deps_paths[] = {
        "../lib/libembedded-ruby.deps",
        "./libembedded-ruby.deps",
        "libembedded-ruby.deps",
        NULL
    };
    const char* lib_paths[] = {
        "../lib/libembedded-ruby.so",
        "./libembedded-ruby.so",
        "libembedded-ruby.so",
        NULL
    };

    if (ruby_api_bootstrap(&g_ruby_api, deps_paths, lib_paths, layout->native_libs_dir) != 0) {
        diag("FAIL: ruby_api_bootstrap");
        goto cleanup;
    }

    LogListener listener = {
        .context = NULL,
        .user_data = NULL,
        .on_log_message = on_log,
    };

    interp = g_ruby_api.interpreter.create(".", layout->ruby_stdlib_path,
                                           layout->native_libs_dir, listener);
    if (interp == NULL) {
        diag("FAIL: interpreter.create");
        goto cleanup;
    }

    RubyScript* s = g_ruby_api.script.create_from_content(CHECK_SCRIPT, strlen(CHECK_SCRIPT));
    if (s == NULL) {
        diag("FAIL: script alloc");
        goto cleanup;
    }
    int rc = g_ruby_api.interpreter.execute_sync(interp, s);
    g_ruby_api.script.destroy(s);
    if (rc != 0) {
        diag("FAIL: requiring the extracted file exited with %d", rc);
        goto cleanup;
    }

    diag("PASS: an extra ZIP installs at the load path root and changes the installation version");
    test_rc = 0;

cleanup:
    if (interp != NULL) g_ruby_api.interpreter.destroy(interp);
    if (layout != NULL) assets_free_layout(layout);
    ruby_api_unload(&g_ruby_api);
    free(zip);
    return test_rc;
}
