#ifndef USE_DIRECT_MEMORY_H
#define USE_DIRECT_MEMORY_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Get the content of an embedded file directly from memory.
 *
 * This function provides access to files that were embedded at compile time
 * using objcopy. The returned pointer points to read-only memory and should
 * not be modified or freed.
 *
 * The content is guaranteed to be null-terminated if it was embedded as a
 * text file, or you must know the size for binary files.
 *
 * @param filename Name of the embedded file (e.g., "fifo_interpreter.rb")
 * @return Pointer to the file content in memory, or NULL if not found
 *
 * @note The returned pointer is valid for the lifetime of the program.
 * @note Do not attempt to free() the returned pointer.
 */
const char* get_in_memory_file_content(const char* filename);

/**
 * Get the size of an embedded file in memory.
 *
 * This is particularly useful for binary files where you need to know
 * the exact size, as the content may contain embedded null bytes.
 *
 * @param filename Name of the embedded file
 * @return Size of the file in bytes, or 0 if not found
 */
size_t get_in_memory_file_size(const char* filename);

/**
 * Check if a file exists in memory.
 *
 * @param filename Name of the file to check
 * @return 1 if file exists in memory, 0 otherwise
 */
int is_file_in_memory(const char* filename);

#ifdef __cplusplus
}
#endif

#endif // USE_DIRECT_MEMORY_H
