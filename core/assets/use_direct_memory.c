#include "use_direct_memory.h"
#include <string.h>

// External symbols created by objcopy for embedded files
// These need to be declared for each embedded file

// For fifo_interpreter.rb
extern const char _binary_fifo_interpreter_rb_start[];
extern const char _binary_fifo_interpreter_rb_end[];

// Add more extern declarations for other embedded files as needed

// Structure to map filenames to their embedded symbols
typedef struct {
    const char* filename;
    const char* start;
    const char* end;
} embedded_file_t;

// Registry of all embedded files
static const embedded_file_t embedded_files[] = {
        {
                .filename = "fifo_interpreter.rb",
                .start = _binary_fifo_interpreter_rb_start,
                .end = _binary_fifo_interpreter_rb_end
        },
        // Add more entries here as you embed more files

        // Sentinel entry
        { .filename = NULL, .start = NULL, .end = NULL }
};

const char* get_in_memory_file_content(const char* filename) {
    if (filename == NULL) {
        return NULL;
    }

    for (const embedded_file_t* file = embedded_files; file->filename != NULL; file++) {
        if (strcmp(file->filename, filename) == 0) {
            return file->start;
        }
    }

    return NULL;
}

size_t get_in_memory_file_size(const char* filename) {
    if (filename == NULL) {
        return 0;
    }

    for (const embedded_file_t* file = embedded_files; file->filename != NULL; file++) {
        if (strcmp(file->filename, filename) == 0) {
            return (size_t)(file->end - file->start);
        }
    }

    return 0;
}

int is_file_in_memory(const char* filename) {
    return get_in_memory_file_content(filename) != NULL ? 1 : 0;
}
