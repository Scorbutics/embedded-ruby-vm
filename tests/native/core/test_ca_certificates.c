/*
 * CA certificates bundle (ca-certificates.c), what SSL_CERT_FILE points to on
 * Android:
 *   - every regular certificate file of the directory is in the bundle, each
 *     starting on its own line; dot files and sub-directories are not;
 *   - the bundle is not rewritten while the directory is older than it;
 *   - a directory without certificates leaves no bundle (nor temporary file).
 *
 * The tests do NOT load the Ruby VM: they call ca-certificates.c directly.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

#include "embedded-ruby-vm/ca-certificates.h"

#define TEST_DIR "./test-ca-certificates"
#define CERTS_DIR TEST_DIR "/cacerts"
#define EMPTY_DIR TEST_DIR "/empty"
#define BUNDLE TEST_DIR "/cacert.pem"

static int g_failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_failures++; \
    } \
} while (0)

static void write_file(const char* path, const char* content) {
    FILE* f = fopen(path, "wb");
    fputs(content, f);
    fclose(f);
}

static char* read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    static char buffer[4096];
    size_t size = fread(buffer, 1, sizeof(buffer) - 1, f);
    buffer[size] = '\0';
    fclose(f);
    return buffer;
}

// Set a path modification time in the past, so that a bundle written now is newer
static void age(const char* path) {
    struct utimbuf times = { .actime = time(NULL) - 3600, .modtime = time(NULL) - 3600 };
    utime(path, &times);
}

static void cleanup(void) {
    remove(CERTS_DIR "/1a2b3c4d.0");
    remove(CERTS_DIR "/5e6f7a8b.0");
    remove(CERTS_DIR "/.hidden");
    rmdir(CERTS_DIR "/subdir");
    rmdir(CERTS_DIR);
    rmdir(EMPTY_DIR);
    remove(BUNDLE);
    remove(BUNDLE ".tmp");
    rmdir(TEST_DIR);
}

static void test_bundle_has_every_certificate(void) {
    // No trailing new line: the next certificate must still start on its own line
    write_file(CERTS_DIR "/1a2b3c4d.0", "-----BEGIN CERTIFICATE-----\nFIRST\n-----END CERTIFICATE-----");
    write_file(CERTS_DIR "/5e6f7a8b.0", "-----BEGIN CERTIFICATE-----\nSECOND\n-----END CERTIFICATE-----\n");
    write_file(CERTS_DIR "/.hidden", "HIDDEN");
    mkdir(CERTS_DIR "/subdir", 0755);
    age(CERTS_DIR);

    CHECK(ca_certificates_write_bundle(CERTS_DIR, BUNDLE) == 0);
    const char* bundle = read_file(BUNDLE);
    CHECK(bundle != NULL);
    if (!bundle) {
        return;
    }
    CHECK(strstr(bundle, "FIRST") != NULL);
    CHECK(strstr(bundle, "SECOND") != NULL);
    CHECK(strstr(bundle, "HIDDEN") == NULL);
    CHECK(strstr(bundle, "-----END CERTIFICATE----------BEGIN") == NULL);
    CHECK(access(BUNDLE ".tmp", F_OK) != 0);
}

static void test_bundle_is_kept_while_the_directory_is_older(void) {
    write_file(BUNDLE, "KEPT");
    CHECK(ca_certificates_write_bundle(CERTS_DIR, BUNDLE) == 0);
    CHECK(strcmp(read_file(BUNDLE), "KEPT") == 0);

    // A system update changes the directory after the bundle was written
    age(BUNDLE);
    struct utimbuf now = { .actime = time(NULL), .modtime = time(NULL) };
    utime(CERTS_DIR, &now);
    CHECK(ca_certificates_write_bundle(CERTS_DIR, BUNDLE) == 0);
    CHECK(strstr(read_file(BUNDLE), "FIRST") != NULL);
}

static void test_no_bundle_without_certificates(void) {
    remove(BUNDLE);
    mkdir(EMPTY_DIR, 0755);
    CHECK(ca_certificates_write_bundle(EMPTY_DIR, BUNDLE) == -1);
    CHECK(access(BUNDLE, F_OK) != 0);
    CHECK(access(BUNDLE ".tmp", F_OK) != 0);
    CHECK(ca_certificates_write_bundle(TEST_DIR "/missing", BUNDLE) == -1);
}

int main(void) {
    cleanup();
    mkdir(TEST_DIR, 0755);
    mkdir(CERTS_DIR, 0755);

    test_bundle_has_every_certificate();
    test_bundle_is_kept_while_the_directory_is_older();
    test_no_bundle_without_certificates();

    cleanup();
    if (g_failures > 0) {
        fprintf(stderr, "test_ca_certificates: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("test_ca_certificates: all tests passed\n");
    return 0;
}
