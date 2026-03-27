cmake_minimum_required(VERSION 3.18)

project(rgss_runtime_wrapper C CXX)

# Android: Override default linker flags to allow undefined symbols
# The Android Gradle Plugin adds --no-undefined by default, but we need to allow
# undefined POSIX symbols (nl_langinfo, setpwent, etc.) that don't exist in Bionic
if(ANDROID)
    # Remove --no-undefined and --fatal-warnings from CMAKE defaults
    # string(REPLACE "-Wl,--no-undefined" "" CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")
    # string(REPLACE "-Wl,--fatal-warnings" "" CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")
    # Add warning instead of error for undefined symbols
    # set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--warn-unresolved-symbols")
endif()

# Get the target ABI from Gradle
if(NOT TARGET_ABI)
    message(FATAL_ERROR "TARGET_ABI must be set (e.g., x86_64, arm64-v8a)")
endif()

# TARGET_ARCH is the filesystem directory name (e.g., arm64, x86_64)
# TARGET_ABI is the Android ABI name (e.g., arm64-v8a, x86_64)
if(NOT TARGET_ARCH)
    set(TARGET_ARCH "${TARGET_ABI}")
endif()

# Construct path to the fat static library using filesystem arch name
set(FAT_LIB_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../build/target/${TARGET_ARCH}-android/usr/local/lib/librgss_runtime.a")

message(STATUS "Target ABI: ${TARGET_ABI}")
message(STATUS "Target Arch (filesystem): ${TARGET_ARCH}")
message(STATUS "Creating shared library wrapper for: ${FAT_LIB_PATH}")

if(NOT EXISTS "${FAT_LIB_PATH}")
    message(FATAL_ERROR "Fat library not found at: ${FAT_LIB_PATH}")
endif()

# Create shared library target with native_main.c
# This provides a generic main() entry point for NativeActivity usage.
# When loaded by an Android NativeActivity, SFML's ANativeActivity_onCreate
# (from sfml-main.a in the fat library) sets up the rendering context and
# spawns a thread that calls main(). The main() reads environment variables
# to find the Ruby script to execute.
# In regular Activity usage, main() is never called.
add_library(rgss_runtime SHARED "${CMAKE_CURRENT_SOURCE_DIR}/native_main.c")

# Link the entire fat static library into the shared library
# Use whole-archive to ensure all symbols are included
if(ANDROID)
    target_link_libraries(rgss_runtime PRIVATE
        -Wl,--whole-archive
        ${FAT_LIB_PATH}
        -Wl,--no-whole-archive
        log       # Android logging
        z         # zlib
        m         # math
        c++       # C++ standard library (required for C++ code in static lib)
        OpenSLES  # OpenSL ES audio (required for OpenAL-Soft backend)
        android   # Android native API (ALooper, etc.)
    )
else()
    target_link_libraries(rgss_runtime PRIVATE
        -Wl,--whole-archive
        ${FAT_LIB_PATH}
        -Wl,--no-whole-archive
        z
        m
        stdc++
    )
endif()

# Set output name to match expected library name
set_target_properties(rgss_runtime PROPERTIES
    OUTPUT_NAME "rgss_runtime"
    VERSION 1.0.0
    SOVERSION 1
)

# Android Gradle Plugin will automatically pick up the built library
