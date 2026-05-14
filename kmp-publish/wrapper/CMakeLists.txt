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

# Get the target ABI from Gradle / Makefile
if(NOT TARGET_ABI)
    message(FATAL_ERROR "TARGET_ABI must be set (e.g., x86_64, arm64-v8a, linux-x86_64, ios-arm64)")
endif()

# TARGET_ARCH is the filesystem directory name (e.g., arm64, x86_64)
# TARGET_ABI is the platform ABI name (e.g., arm64-v8a, x86_64)
if(NOT TARGET_ARCH)
    set(TARGET_ARCH "${TARGET_ABI}")
endif()

# FAT_LIB_PATH is passed via -D from the Makefile's publish-kmp target.
if(NOT FAT_LIB_PATH)
    message(FATAL_ERROR "FAT_LIB_PATH must be set to the path of the fat static library (librgss_runtime.a)")
endif()

message(STATUS "Target ABI: ${TARGET_ABI}")
message(STATUS "Target Arch (filesystem): ${TARGET_ARCH}")
message(STATUS "Creating wrapper library for: ${FAT_LIB_PATH}")

if(NOT EXISTS "${FAT_LIB_PATH}")
    message(FATAL_ERROR "Fat library not found at: ${FAT_LIB_PATH}")
endif()

# ---------------------------------------------------------------------------
# iOS: Static framework (no .so wrapping needed, just validate & copy)
# ---------------------------------------------------------------------------
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    # iOS uses static linking exclusively.
    # We don't need to wrap the .a into a .so — Kotlin/Native links it directly.
    # This CMake target just validates the fat library exists and copies it
    # to a known output path for the Gradle build to consume.
    add_custom_target(rgss_runtime_ios ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${FAT_LIB_PATH}"
            "${CMAKE_BINARY_DIR}/librgss_runtime.a"
        COMMENT "Staging iOS fat library: ${FAT_LIB_PATH}"
    )
    return()
endif()

# ---------------------------------------------------------------------------
# Android & Linux: Shared library wrapping the fat static lib
# ---------------------------------------------------------------------------

# Create shared library target with native_main.c
# This provides a generic main() entry point for NativeActivity usage (Android)
# or can be used as a shared library entry point (Linux Desktop).
add_library(rgss_runtime SHARED "${CMAKE_CURRENT_SOURCE_DIR}/native_main.c")

# native_main.c uses RubyVMRemoteDebugOptions / RubyVMRemoteEvalOptions from
# the public header — these are POD structs only, no pthread/atomic, so this
# is the only embedded-ruby-vm path the wrapper needs.
target_include_directories(rgss_runtime PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include/public"
)

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
elseif(APPLE)
    # macOS uses -force_load instead of --whole-archive
    # macOS provides dl and pthread in libSystem (no separate libraries)
    #
    # -force_load pulls ALL objects from the fat archive, so we must link
    # every system framework / library that any constituent library needs:
    #   SFML        → Cocoa, IOKit, CoreFoundation, CoreVideo, Carbon, OpenGL
    #   OpenAL-Soft → AudioToolbox, CoreAudio, AudioUnit
    #   Ruby / misc → compression, iconv
    target_link_libraries(rgss_runtime PRIVATE
        -Wl,-force_load,${FAT_LIB_PATH}
        z m stdc++
        "-framework AudioToolbox"
        "-framework AudioUnit"
        "-framework CoreAudio"
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreFoundation"
        "-framework CoreVideo"
        "-framework Carbon"
        "-framework OpenGL"
        compression
        iconv
        bz2
    )
else()
    # Linux
    target_link_libraries(rgss_runtime PRIVATE
        -Wl,--whole-archive
        ${FAT_LIB_PATH}
        -Wl,--no-whole-archive
        z m dl pthread stdc++ X11 Xrandr Xcursor Xi GL udev
    )
endif()

# Set output name to match expected library name
set_target_properties(rgss_runtime PROPERTIES
    OUTPUT_NAME "rgss_runtime"
    VERSION 1.0.0
    SOVERSION 1
)
