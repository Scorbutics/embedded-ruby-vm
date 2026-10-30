#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "embedded-ruby-vm/ca-certificates.h"

#define CA_CERTIFICATES_BUNDLE_NAME "cacert.pem"

#ifdef __ANDROID__
// Android 14+ stores the certificates in the (updatable) conscrypt APEX
static const char* const SYSTEM_CERTIFICATES_DIRECTORIES[] = {
    "/apex/com.android.conscrypt/cacerts",
    "/system/etc/security/cacerts",
    NULL
};
#endif

// Append a file content to an opened stream
static int append_file(FILE* out, const char* path) {
    FILE* in = fopen(path, "rb");
    if (!in) {
        return -1;
    }
    char buffer[4096];
    size_t read;
    int result = 0;
    while ((read = fread(buffer, 1, sizeof(buffer), in)) > 0) {
        if (fwrite(buffer, 1, read, out) != read) {
            result = -1;
            break;
        }
    }
    fclose(in);
    // A PEM block must start on its own line
    return result == 0 && fputc('\n', out) != EOF ? 0 : -1;
}

int ca_certificates_write_bundle(const char* certificatesDirectory, const char* bundlePath) {
    struct stat directoryStat;
    struct stat bundleStat;
    if (stat(certificatesDirectory, &directoryStat) != 0) {
        return -1;
    }
    if (stat(bundlePath, &bundleStat) == 0 && bundleStat.st_mtime > directoryStat.st_mtime) {
        return 0;
    }

    DIR* directory = opendir(certificatesDirectory);
    if (!directory) {
        return -1;
    }
    const size_t pathSize = strlen(bundlePath) + 5;
    char* temporaryPath = malloc(pathSize);
    if (!temporaryPath) {
        closedir(directory);
        return -1;
    }
    snprintf(temporaryPath, pathSize, "%s.tmp", bundlePath);
    FILE* out = fopen(temporaryPath, "wb");
    if (!out) {
        free(temporaryPath);
        closedir(directory);
        return -1;
    }

    int certificateCount = 0;
    char certificatePath[1024];
    struct dirent* entry;
    while ((entry = readdir(directory)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        snprintf(certificatePath, sizeof(certificatePath), "%s/%s", certificatesDirectory, entry->d_name);
        struct stat certificateStat;
        // An unreadable certificate is skipped, the others still make the bundle
        if (stat(certificatePath, &certificateStat) == 0 && S_ISREG(certificateStat.st_mode) &&
            append_file(out, certificatePath) == 0) {
            certificateCount++;
        }
    }
    closedir(directory);

    int result = fclose(out) == 0 && certificateCount > 0 && rename(temporaryPath, bundlePath) == 0 ? 0 : -1;
    if (result != 0) {
        remove(temporaryPath);
    }
    free(temporaryPath);
    return result;
}

int ca_certificates_setup(const char* baseDirectory) {
#ifdef __ANDROID__
    if (getenv("SSL_CERT_FILE") != NULL) {
        return 0;
    }
    const char* certificatesDirectory = NULL;
    struct stat directoryStat;
    for (const char* const* candidate = SYSTEM_CERTIFICATES_DIRECTORIES; *candidate != NULL; candidate++) {
        if (stat(*candidate, &directoryStat) == 0 && S_ISDIR(directoryStat.st_mode)) {
            certificatesDirectory = *candidate;
            break;
        }
    }
    if (!certificatesDirectory) {
        return -1;
    }

    char bundlePath[1024];
    snprintf(bundlePath, sizeof(bundlePath), "%s/" CA_CERTIFICATES_BUNDLE_NAME, baseDirectory);
    if (ca_certificates_write_bundle(certificatesDirectory, bundlePath) != 0) {
        return -1;
    }
    return setenv("SSL_CERT_FILE", bundlePath, 1) == 0 ? 1 : -1;
#else
    (void) baseDirectory;
    return 0;
#endif
}
