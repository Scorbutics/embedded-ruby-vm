#!/bin/bash
# Extract library dependencies recursively from a shared library
# Usage: extract_deps.sh <library_path> <search_dir> [output_file]
# Outputs C array entries to stdout or to output_file if specified

# Temporarily disable set -e to debug
# set -e

LIB_PATH="$1"
SEARCH_DIR="$2"
OUTPUT_FILE="$3"

if [ -z "$LIB_PATH" ] || [ -z "$SEARCH_DIR" ]; then
    echo "Usage: $0 <library_path> <search_dir> [output_file]" >&2
    exit 1
fi

# Verify library file exists
if [ ! -f "$LIB_PATH" ]; then
    echo "Error: Library file not found: $LIB_PATH" >&2
    exit 1
fi

# Verify search directory exists
if [ ! -d "$SEARCH_DIR" ]; then
    echo "Error: Search directory not found: $SEARCH_DIR" >&2
    exit 1
fi

# Track visited libraries to avoid cycles
declare -A visited

# Recursive function to collect dependencies
collect_deps() {
    local lib="$1"

    # Skip if already visited
    if [ "${visited[$lib]}" = "1" ]; then
        return
    fi
    visited[$lib]=1

    # Find full path
    local lib_full_path=""
    if [ -f "$lib" ]; then
        lib_full_path="$lib"
    elif [ -f "$SEARCH_DIR/$lib" ]; then
        lib_full_path="$SEARCH_DIR/$lib"
    else
        # Library not found in search dir, might be system lib - skip
        return
    fi

    # Check if it's an ELF file (check magic bytes directly)
    # ELF files start with 0x7F 'E' 'L' 'F'
    if ! head -c 4 "$lib_full_path" 2>/dev/null | grep -q "^.ELF"; then
        return
    fi

    # Get dependencies of this library
    local deps=$(readelf -d "$lib_full_path" 2>/dev/null | grep "NEEDED" | sed 's/.*\[\(.*\)\]/\1/' || true)

    # Recursively process dependencies first (depth-first for correct load order)
    for dep in $deps; do
        collect_deps "$dep"
    done

    # Add this library to output (dependencies come first)
    local libname=$(basename "$lib")
    echo "    \"$libname\","
}

# Redirect output to file if specified
if [ -n "$OUTPUT_FILE" ]; then
    exec > "$OUTPUT_FILE"
fi

# Start collection from main library
if [ -f "$LIB_PATH" ]; then
    collect_deps "$LIB_PATH"
else
    # Library doesn't exist yet (building for the first time)
    # Output empty dependency list
    :
fi
