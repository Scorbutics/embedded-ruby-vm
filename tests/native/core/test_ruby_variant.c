/*
 * Regression test for the embedded Ruby variant (3.0, 3.2 or 3.3).
 *
 * The Ruby ABI version used to be hard-coded ("3.1.0") in assets/install.c,
 * so a build against another Ruby extracted its stdlib under one directory and
 * looked it up under another. It now comes from the downloaded headers; this
 * test checks the pieces agree for whatever variant the build was configured
 * with (EXPECTED_RUBY_MINOR, from RUBY_VARIANT):
 *   - the stdlib extension directory reported by the layout exists after
 *     extraction (ruby/<abi>/ in the archives matches the ABI install.c uses);
 *   - the VM runs the expected Ruby minor;
 *   - the extensions whose native library moved between Ruby versions load
 *     and work: psych (libyaml), fiddle (libffi), plus openssl and zlib.
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

#ifndef EXPECTED_RUBY_MINOR
#error "EXPECTED_RUBY_MINOR must be defined (e.g. \"3.0\")"
#endif

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

/* Each check exits with its own code so a failure names the culprit; success
 * falls off the end (0), since an explicit exit(0) is reported as 1. */
static const char* CHECK_SCRIPT =
    "puts \"Ruby #{RUBY_VERSION} on #{RUBY_PLATFORM}\"\n"
    "exit(10) unless RUBY_VERSION.start_with?('" EXPECTED_RUBY_MINOR ".')\n"
    "begin\n"
    "  require 'yaml'\n"
    "  exit(11) unless YAML.load(\"a: [1, 2]\") == { 'a' => [1, 2] }\n"
    "  exit(12) unless YAML.dump({ 'b' => 3 }).include?('b: 3')\n"
    "rescue LoadError, StandardError => e\n"
    "  puts \"psych: #{e.class}: #{e.message}\"\n"
    "  exit(13)\n"
    "end\n"
    "begin\n"
    "  require 'fiddle'\n"
    "  strlen = Fiddle::Function.new(Fiddle::Handle::DEFAULT['strlen'],\n"
    "                                [Fiddle::TYPE_VOIDP], Fiddle::TYPE_SIZE_T)\n"
    "  exit(14) unless strlen.call('hello') == 5\n"
    "rescue LoadError, StandardError => e\n"
    "  puts \"fiddle: #{e.class}: #{e.message}\"\n"
    "  exit(15)\n"
    "end\n"
    "begin\n"
    "  require 'openssl'\n"
    "  exit(16) unless OpenSSL::Digest::SHA256.hexdigest('') ==\n"
    "    'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855'\n"
    "  require 'zlib'\n"
    "  exit(17) unless Zlib::Inflate.inflate(Zlib::Deflate.deflate('abc')) == 'abc'\n"
    "rescue LoadError, StandardError => e\n"
    "  puts \"openssl/zlib: #{e.class}: #{e.message}\"\n"
    "  exit(18)\n"
    "end\n";

int main(void) {
    int test_rc = 1;
    AssetsLayout* layout = NULL;
    AssetsError assets_error;
    RubyInterpreter* interp = NULL;
    struct stat st;

    assets_error_init(&assets_error);
    layout = assets_bootstrap("./test-ruby-install-variant", &assets_error);
    if (layout == NULL) {
        diag("FAIL: bootstrap: %s", assets_error.message);
        return 1;
    }

    diag("DIAG: expected Ruby %s, stdlib ext path %s", EXPECTED_RUBY_MINOR, layout->ruby_stdlib_ext_path);
    if (stat(layout->ruby_stdlib_ext_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        diag("FAIL: stdlib ext directory %s was not extracted (ABI mismatch with the archives?)",
             layout->ruby_stdlib_ext_path);
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
        diag("FAIL: variant check script exited with %d (see the exit codes in CHECK_SCRIPT)", rc);
        goto cleanup;
    }

    diag("PASS: Ruby %s variant installs, runs and loads psych/fiddle/openssl/zlib", EXPECTED_RUBY_MINOR);
    test_rc = 0;

cleanup:
    if (interp != NULL) g_ruby_api.interpreter.destroy(interp);
    if (layout != NULL) assets_free_layout(layout);
    ruby_api_unload(&g_ruby_api);
    return test_rc;
}
