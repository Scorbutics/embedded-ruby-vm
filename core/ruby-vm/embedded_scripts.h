#ifndef EMBEDDED_SCRIPTS_H
#define EMBEDDED_SCRIPTS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Get the content of an embedded script directly from memory.
 *
 * This function provides access to Ruby scripts that were embedded at compile time
 * into libembedded-ruby using objcopy. The returned pointer points to read-only
 * memory and should not be modified or freed.
 *
 * This is similar to assets' use_direct_memory.h but specifically for scripts
 * embedded within the ruby-vm library itself (like fifo_interpreter.rb).
 *
 * @param script_name Name of the embedded script (e.g., "fifo_interpreter.rb")
 * @return Pointer to the script content in memory, or NULL if not found
 *
 * @note The returned pointer is valid for the lifetime of the program.
 * @note Do not attempt to free() the returned pointer.
 */
const char* embedded_script_get_content(const char* script_name);

/**
 * Get the size of an embedded script in memory.
 *
 * @param script_name Name of the embedded script
 * @return Size of the script in bytes, or 0 if not found
 */
size_t embedded_script_get_size(const char* script_name);

/**
 * Check if a script exists in embedded memory.
 *
 * @param script_name Name of the script to check
 * @return 1 if script exists in memory, 0 otherwise
 */
int embedded_script_exists(const char* script_name);

#ifdef __cplusplus
}
#endif

#endif // EMBEDDED_SCRIPTS_H
