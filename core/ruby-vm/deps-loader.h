#ifndef DEPS_LOADER_H
#define DEPS_LOADER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

/**
 * Load library dependencies from a .deps file
 *
 * The .deps file format contains C-style string entries like:
 *     "libruby.so.3.1",
 *     "libssl.so",
 *
 * This function parses the file and loads each library from the specified directory.
 *
 * @param deps_file Path to the .deps file (e.g., "libembedded-ruby.deps")
 * @param libs_dir Directory containing the libraries
 * @return 0 on success, -1 on error
 */
static inline int load_dependencies_from_file(const char* deps_file, const char* libs_dir) {
    FILE* f = fopen(deps_file, "r");
    if (!f) {
        fprintf(stderr, "Cannot open deps file: %s\n", deps_file);
        return -1;
    }

    char line[512];
    int loaded_count = 0;
    int failed_count = 0;

    while (fgets(line, sizeof(line), f)) {
        // Skip empty lines and comments
        if (line[0] == '\n' || line[0] == '#' || line[0] == '/') {
            continue;
        }

        // Extract library name from: "libname.so",
        char* start = strchr(line, '"');
        if (!start) continue;
        start++; // Skip opening quote

        char* end = strchr(start, '"');
        if (!end) continue;
        *end = '\0'; // Null-terminate

        // Build full path
        char lib_path[1024];
        snprintf(lib_path, sizeof(lib_path), "%s/%s", libs_dir, start);

        // Load with RTLD_NOW | RTLD_GLOBAL
        void* handle = dlopen(lib_path, RTLD_NOW | RTLD_GLOBAL);
        if (handle) {
            printf("  ✓ Loaded: %s\n", start);
            loaded_count++;
        } else {
            // Not necessarily fatal - might be a system library
            const char* error = dlerror();
            printf("  ⚠ Skipped: %s (%s)\n", start, error ? error : "unknown error");
            failed_count++;
            dlerror(); // Clear error
        }
    }

    fclose(f);

    printf("Dependency loading complete: %d loaded, %d skipped\n", loaded_count, failed_count);
    return 0;
}

#endif /* DEPS_LOADER_H */
