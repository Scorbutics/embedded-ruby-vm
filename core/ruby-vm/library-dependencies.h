#ifndef LIBRARY_DEPENDENCIES_H
#define LIBRARY_DEPENDENCIES_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Get the list of shared library dependencies for libembedded-ruby.so
 *
 * This function returns a NULL-terminated array of library names that
 * libembedded-ruby.so depends on. The dependencies are listed in load order
 * (dependencies first, then libraries that depend on them).
 *
 * This is useful for manually loading dependencies via dlopen() before
 * loading libembedded-ruby.so, particularly on Android where LD_LIBRARY_PATH
 * cannot be modified at runtime.
 *
 * @return NULL-terminated array of library names (e.g., "libruby.so", "libssl.so")
 *         The array is statically allocated and should not be freed.
 *         Returns NULL if dependency information is not available.
 *
 * Example usage:
 * @code
 * const char** deps = ruby_vm_get_library_dependencies();
 * if (deps) {
 *     for (int i = 0; deps[i] != NULL; i++) {
 *         char path[512];
 *         snprintf(path, sizeof(path), "%s/%s", native_libs_dir, deps[i]);
 *         dlopen(path, RTLD_NOW | RTLD_GLOBAL);
 *     }
 * }
 * // Now safe to load libembedded-ruby.so
 * @endcode
 */
const char** ruby_vm_get_library_dependencies(void);

#ifdef __cplusplus
}
#endif

#endif /* LIBRARY_DEPENDENCIES_H */
