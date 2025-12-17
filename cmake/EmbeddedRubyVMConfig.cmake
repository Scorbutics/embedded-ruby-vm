# EmbeddedRubyVMConfig.cmake
# CMake configuration package for embedded-ruby-vm
# Provides Ruby runtime, headers, and Kotlin Multiplatform artifacts for consumption by other CMake projects

cmake_minimum_required(VERSION 3.19.2)

# Get the directory where this config file is located
get_filename_component(EMBEDDED_RUBY_VM_CMAKE_DIR "${CMAKE_CURRENT_LIST_FILE}" PATH)
get_filename_component(EMBEDDED_RUBY_VM_ROOT_DIR "${EMBEDDED_RUBY_VM_CMAKE_DIR}/.." ABSOLUTE)

message(STATUS "Found EmbeddedRubyVM: ${EMBEDDED_RUBY_VM_ROOT_DIR}")

# ============================================================================
# Version Information
# ============================================================================

set(EMBEDDED_RUBY_VM_VERSION "3.1.1" CACHE STRING "Ruby version embedded in embedded-ruby-vm")
set(EMBEDDED_RUBY_VM_VERSION_MAJOR "3")
set(EMBEDDED_RUBY_VM_VERSION_MINOR "1")
set(EMBEDDED_RUBY_VM_VERSION_PATCH "1")

# ============================================================================
# Platform Detection
# ============================================================================

# Detect host platform and architecture
# This matches the naming convention used by ruby-for-android and embedded-ruby-vm
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" ARCH_LOWER)
string(TOLOWER "${CMAKE_SYSTEM_NAME}" PLATFORM_LOWER)

# Normalize architecture names
if(ARCH_LOWER MATCHES "^(amd64|x86_64|x64)$")
    set(ARCH_NORMALIZED "x86_64")
elseif(ARCH_LOWER MATCHES "^(aarch64|arm64)$")
    set(ARCH_NORMALIZED "aarch64")
elseif(ARCH_LOWER MATCHES "^(armv7|armv7a|armeabi-v7a)$")
    set(ARCH_NORMALIZED "armv7")
else()
    set(ARCH_NORMALIZED "${ARCH_LOWER}")
endif()

# Normalize platform names
if(PLATFORM_LOWER STREQUAL "android")
    set(PLATFORM_NORMALIZED "android")
elseif(PLATFORM_LOWER STREQUAL "linux")
    set(PLATFORM_NORMALIZED "linux")
elseif(PLATFORM_LOWER STREQUAL "darwin")
    set(PLATFORM_NORMALIZED "darwin")
elseif(PLATFORM_LOWER STREQUAL "ios")
    set(PLATFORM_NORMALIZED "ios")
else()
    set(PLATFORM_NORMALIZED "${PLATFORM_LOWER}")
endif()

# Construct HOST string (matches ruby-for-android convention)
set(EMBEDDED_RUBY_VM_HOST "${ARCH_NORMALIZED}-${PLATFORM_NORMALIZED}-${PLATFORM_NORMALIZED}")

# Ruby uses slightly different architecture naming (matches rbconfig.rb RUBY_PLATFORM)
set(EMBEDDED_RUBY_VM_RUBY_ARCH "${ARCH_NORMALIZED}-${PLATFORM_NORMALIZED}")

message(STATUS "EmbeddedRubyVM: Detected HOST=${EMBEDDED_RUBY_VM_HOST}, RUBY_ARCH=${EMBEDDED_RUBY_VM_RUBY_ARCH}")

# ============================================================================
# Ruby Runtime Paths
# ============================================================================

# Ruby native libraries directory
set(EMBEDDED_RUBY_VM_NATIVE_LIBS "${EMBEDDED_RUBY_VM_ROOT_DIR}/core/external/lib/${EMBEDDED_RUBY_VM_HOST}")

# Ruby headers directory
set(EMBEDDED_RUBY_VM_INCLUDE_DIRS
    "${EMBEDDED_RUBY_VM_ROOT_DIR}/core/external/include/ruby"
    "${EMBEDDED_RUBY_VM_ROOT_DIR}/core/external/include/${EMBEDDED_RUBY_VM_HOST}/ruby"
)

# Ruby library directory (for linking)
set(EMBEDDED_RUBY_VM_LIBRARY_DIRS "${EMBEDDED_RUBY_VM_NATIVE_LIBS}")

# Ruby standard library directory (Ruby source files)
# Note: This would typically be in a stdlib/ directory, but embedded-ruby-vm
# packages Ruby stdlib inside the assets archive
set(EMBEDDED_RUBY_VM_STDLIB_DIR "${EMBEDDED_RUBY_VM_ROOT_DIR}/assets")

# ============================================================================
# Kotlin Multiplatform Artifacts
# ============================================================================

# Gradle build output directory
set(EMBEDDED_RUBY_VM_GRADLE_BUILD_DIR "${EMBEDDED_RUBY_VM_ROOT_DIR}/build")
set(EMBEDDED_RUBY_VM_KMP_BUILD_DIR "${EMBEDDED_RUBY_VM_ROOT_DIR}/kmp/build")

# Kotlin artifacts (JARs, AARs) - these are produced by Gradle
# Note: Actual paths depend on Gradle build configuration
# Common locations:
#   - Desktop JVM: kmp/build/libs/*.jar
#   - Android: kmp/build/outputs/aar/*.aar
set(EMBEDDED_RUBY_VM_KOTLIN_LIBS
    "${EMBEDDED_RUBY_VM_KMP_BUILD_DIR}/libs"
    "${EMBEDDED_RUBY_VM_KMP_BUILD_DIR}/outputs"
)

# ============================================================================
# Build Mode Detection
# ============================================================================

# Allow users to explicitly select static or shared linking
# Usage: cmake -DEMBEDDED_RUBY_VM_USE_STATIC=ON ...
option(EMBEDDED_RUBY_VM_USE_STATIC "Force static linking of Ruby libraries" OFF)

# Check if static or shared libraries are available
if(EXISTS "${EMBEDDED_RUBY_VM_NATIVE_LIBS}/libruby.so" OR
   EXISTS "${EMBEDDED_RUBY_VM_NATIVE_LIBS}/libruby.so.3.1" OR
   EXISTS "${EMBEDDED_RUBY_VM_NATIVE_LIBS}/libruby.dylib")
    set(EMBEDDED_RUBY_VM_SHARED_AVAILABLE TRUE)
else()
    set(EMBEDDED_RUBY_VM_SHARED_AVAILABLE FALSE)
endif()

if(EXISTS "${EMBEDDED_RUBY_VM_NATIVE_LIBS}/libruby-static.a" OR
   EXISTS "${EMBEDDED_RUBY_VM_NATIVE_LIBS}/libruby.a")
    set(EMBEDDED_RUBY_VM_STATIC_AVAILABLE TRUE)
else()
    set(EMBEDDED_RUBY_VM_STATIC_AVAILABLE FALSE)
endif()

# Determine which library type to use based on user preference and availability
if(EMBEDDED_RUBY_VM_USE_STATIC)
    if(NOT EMBEDDED_RUBY_VM_STATIC_AVAILABLE)
        message(FATAL_ERROR "EmbeddedRubyVM: Static libraries requested but not found in ${EMBEDDED_RUBY_VM_NATIVE_LIBS}")
    endif()
    set(EMBEDDED_RUBY_VM_LINK_TYPE "STATIC")
    message(STATUS "EmbeddedRubyVM: Using STATIC libraries (explicitly requested)")
else()
    # Default behavior: prefer shared, fallback to static
    if(EMBEDDED_RUBY_VM_SHARED_AVAILABLE)
        set(EMBEDDED_RUBY_VM_LINK_TYPE "SHARED")
        message(STATUS "EmbeddedRubyVM: Using SHARED libraries (default)")
    elseif(EMBEDDED_RUBY_VM_STATIC_AVAILABLE)
        set(EMBEDDED_RUBY_VM_LINK_TYPE "STATIC")
        message(STATUS "EmbeddedRubyVM: Using STATIC libraries (fallback)")
    else()
        set(EMBEDDED_RUBY_VM_LINK_TYPE "UNKNOWN")
        message(WARNING "EmbeddedRubyVM: No libraries found")
    endif()
endif()

# ============================================================================
# Imported Targets
# ============================================================================

# Create imported target for Ruby library
if(NOT TARGET EmbeddedRubyVM::ruby)
    if(EMBEDDED_RUBY_VM_LINK_TYPE STREQUAL "SHARED")
        add_library(EmbeddedRubyVM::ruby SHARED IMPORTED)

        # Find the shared library (try different naming conventions)
        if(EXISTS "${EMBEDDED_RUBY_VM_NATIVE_LIBS}/libruby.so")
            set(_ruby_lib "${EMBEDDED_RUBY_VM_NATIVE_LIBS}/libruby.so")
        elseif(EXISTS "${EMBEDDED_RUBY_VM_NATIVE_LIBS}/libruby.so.3.1")
            set(_ruby_lib "${EMBEDDED_RUBY_VM_NATIVE_LIBS}/libruby.so.3.1")
        elseif(EXISTS "${EMBEDDED_RUBY_VM_NATIVE_LIBS}/libruby.dylib")
            set(_ruby_lib "${EMBEDDED_RUBY_VM_NATIVE_LIBS}/libruby.dylib")
        endif()

        set_target_properties(EmbeddedRubyVM::ruby PROPERTIES
            IMPORTED_LOCATION "${_ruby_lib}"
            INTERFACE_INCLUDE_DIRECTORIES "${EMBEDDED_RUBY_VM_INCLUDE_DIRS}"
        )
        message(STATUS "EmbeddedRubyVM: Linked Ruby library: ${_ruby_lib}")

    elseif(EMBEDDED_RUBY_VM_LINK_TYPE STREQUAL "STATIC")
        add_library(EmbeddedRubyVM::ruby STATIC IMPORTED)

        # Find the static library (try different naming conventions)
        if(EXISTS "${EMBEDDED_RUBY_VM_NATIVE_LIBS}/libruby-static.a")
            set(_ruby_lib "${EMBEDDED_RUBY_VM_NATIVE_LIBS}/libruby-static.a")
        elseif(EXISTS "${EMBEDDED_RUBY_VM_NATIVE_LIBS}/libruby.a")
            set(_ruby_lib "${EMBEDDED_RUBY_VM_NATIVE_LIBS}/libruby.a")
        endif()

        set_target_properties(EmbeddedRubyVM::ruby PROPERTIES
            IMPORTED_LOCATION "${_ruby_lib}"
            INTERFACE_INCLUDE_DIRECTORIES "${EMBEDDED_RUBY_VM_INCLUDE_DIRS}"
        )
        message(STATUS "EmbeddedRubyVM: Linked Ruby library: ${_ruby_lib}")

    else()
        message(WARNING "EmbeddedRubyVM: No Ruby libraries found in ${EMBEDDED_RUBY_VM_NATIVE_LIBS}")
    endif()
endif()

# ============================================================================
# Validation
# ============================================================================

# Verify that required directories exist
if(NOT EXISTS "${EMBEDDED_RUBY_VM_NATIVE_LIBS}")
    message(WARNING "EmbeddedRubyVM: Native libs directory not found: ${EMBEDDED_RUBY_VM_NATIVE_LIBS}")
    message(WARNING "  Make sure to build embedded-ruby-vm for your target platform first:")
    message(WARNING "    cd ${EMBEDDED_RUBY_VM_ROOT_DIR}")
    message(WARNING "    ./gradlew buildNativeLibsDesktop  # or buildNativeLibsAndroid, etc.")
endif()

# Check for Ruby headers
set(_ruby_header_found FALSE)
foreach(_include_dir ${EMBEDDED_RUBY_VM_INCLUDE_DIRS})
    if(EXISTS "${_include_dir}/ruby.h")
        set(_ruby_header_found TRUE)
        break()
    endif()
endforeach()

if(NOT _ruby_header_found)
    message(WARNING "EmbeddedRubyVM: Ruby headers not found in include directories")
    message(WARNING "  Searched in: ${EMBEDDED_RUBY_VM_INCLUDE_DIRS}")
endif()

# ============================================================================
# Summary
# ============================================================================

message(STATUS "EmbeddedRubyVM Configuration:")
message(STATUS "  Version: ${EMBEDDED_RUBY_VM_VERSION}")
message(STATUS "  Ruby Architecture: ${EMBEDDED_RUBY_VM_RUBY_ARCH}")
message(STATUS "  Native Libraries: ${EMBEDDED_RUBY_VM_NATIVE_LIBS}")
message(STATUS "  Include Directories: ${EMBEDDED_RUBY_VM_INCLUDE_DIRS}")
message(STATUS "  Shared Libraries Available: ${EMBEDDED_RUBY_VM_SHARED_AVAILABLE}")
message(STATUS "  Static Libraries Available: ${EMBEDDED_RUBY_VM_STATIC_AVAILABLE}")
message(STATUS "  Link Type: ${EMBEDDED_RUBY_VM_LINK_TYPE}")
message(STATUS "  Kotlin Artifacts: ${EMBEDDED_RUBY_VM_KOTLIN_LIBS}")

# ============================================================================
# Export Variables for Consumers
# ============================================================================

# Mark important variables as advanced (hidden in cmake-gui unless "Advanced" is toggled)
mark_as_advanced(
    EMBEDDED_RUBY_VM_CMAKE_DIR
    EMBEDDED_RUBY_VM_ROOT_DIR
    EMBEDDED_RUBY_VM_GRADLE_BUILD_DIR
    EMBEDDED_RUBY_VM_KMP_BUILD_DIR
)

# Set package as found
set(EmbeddedRubyVM_FOUND TRUE)
