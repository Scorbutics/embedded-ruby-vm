cmake_minimum_required(VERSION 3.19.2)

# Disable RUNPATH BEFORE project() - native libraries will be extracted and loaded at runtime
# instead of using hardcoded paths from the build environment
set(CMAKE_SKIP_RPATH ON)
set(CMAKE_SKIP_BUILD_RPATH ON)

project("embedded-ruby-vm")

# Enable Position Independent Code for all targets
# Required when linking static libraries into shared libraries
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Add NDEBUG for Release builds (applies to all targets)
# This is needed because COMPILE_FLAGS in subdirectories override CMake's defaults
add_compile_definitions($<$<CONFIG:Release>:NDEBUG>)

# Determine host triplet using compiler's -dumpmachine
# This gives us the proper triplet (e.g., x86_64-linux-gnu, aarch64-linux-android)
execute_process(
    COMMAND ${CMAKE_C_COMPILER} -dumpmachine
    OUTPUT_VARIABLE HOST
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

# Fallback if -dumpmachine fails (shouldn't happen with modern compilers)
if(NOT HOST)
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}-${CMAKE_SYSTEM_NAME}" HOST)
    message(WARNING "Could not determine host triplet from compiler, using fallback: ${HOST}")
endif()

message(NOTICE "Configuring for host: ${HOST}")

# Two-flag build system for fine-grained control:
# - BUILD_WRAPPER_SHARED: Controls output type of libassets and libembedded-ruby
#                         (ON for JVM/JNI, OFF for Kotlin/Native)
# - BUILD_SHARED_LIBS: Controls which Ruby libraries to link against
#                      (ON = libruby.so, OFF = libruby-static.a + libruby-ext.a)
option(BUILD_WRAPPER_SHARED "Build libassets and libembedded-ruby as shared libraries (required for JVM/JNI)" OFF)
option(BUILD_SHARED_LIBS "Link against shared Ruby libraries instead of static" OFF)
option(BUILD_JNI "Build JNI binding" OFF)
option(BUILD_JNI_ANDROID_LOG "Build Android Logcat output" OFF)
option(BUILD_TESTS "Build tests" ON)

# Set build type directory for library resolution based on BUILD_SHARED_LIBS
if(BUILD_SHARED_LIBS)
    set(BUILD_TYPE_DIR "shared")
else()
    set(BUILD_TYPE_DIR "static")
endif()

# External libraries directory includes build type (shared/static)
# This allows supporting both shared and static Ruby builds
set(EXTERNAL_LIBS_DIR ${CMAKE_SOURCE_DIR}/external/lib/${HOST}/${BUILD_TYPE_DIR})

# Fallback: if detected HOST directory doesn't exist, try alternative triplets
if(NOT EXISTS "${EXTERNAL_LIBS_DIR}")
    message(WARNING "Directory ${EXTERNAL_LIBS_DIR} not found, trying alternatives...")
    
    # Try x86_64-linux-gnu if HOST is x86_64-linux-linux
    if(HOST MATCHES "x86_64-linux")
        set(ALT_HOST "x86_64-linux-gnu")
        set(EXTERNAL_LIBS_DIR ${CMAKE_SOURCE_DIR}/external/lib/${ALT_HOST}/${BUILD_TYPE_DIR})
        if(EXISTS "${EXTERNAL_LIBS_DIR}")
            message(STATUS "Using alternative host triplet: ${ALT_HOST}")
            set(HOST "${ALT_HOST}")
        endif()
    endif()
endif()

message(STATUS "Using external libraries from: ${EXTERNAL_LIBS_DIR}")
message(STATUS "Build wrapper as shared: ${BUILD_WRAPPER_SHARED}")
message(STATUS "Link against shared Ruby: ${BUILD_SHARED_LIBS}")

# Add external library directory to linker search path
# This allows CMake to find ruby-static, ssl, crypto, readline, etc.
link_directories(${EXTERNAL_LIBS_DIR})

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

add_subdirectory(core)
add_subdirectory(assets)

if(BUILD_JNI)
    set(JNI_DIR ${CMAKE_SOURCE_DIR}/jni)
    add_subdirectory(jni)
endif()

# Create embedded-ruby library with type based on BUILD_WRAPPER_SHARED
if(BUILD_WRAPPER_SHARED)
    add_library(embedded-ruby SHARED dummy.c)
else()
    add_library(embedded-ruby STATIC dummy.c)
endif()

# Platform-specific linking configuration
# Linux: Use --whole-archive for both static and shared builds
#   - Static: Ensures all symbols are available for consumers
#   - Shared: Exports symbols from static dependencies (needed for dlopen)
# macOS: Use -force_load for static, interface library for shared
if(UNIX AND NOT APPLE)
    # Linux: --whole-archive works for both static and shared builds
    if(BUILD_WRAPPER_SHARED)
        # Shared library: Use --whole-archive only on ruby-vm (which transitively includes logging)
        # Allow multiple definitions to handle Init_ext (in both ruby-static and ruby-ext) and libgcc symbols
        target_link_libraries(embedded-ruby PRIVATE
                -Wl,--allow-multiple-definition
                -Wl,--whole-archive
                ruby-vm
                -Wl,--no-whole-archive
                $<$<BOOL:${BUILD_JNI}>:jni>
        )
    else()
        # Static library: Include both logging and ruby-vm with --whole-archive
        target_link_libraries(embedded-ruby PRIVATE
                -Wl,--whole-archive
                logging
                ruby-vm
                -Wl,--no-whole-archive
                $<$<BOOL:${BUILD_JNI}>:jni>
        )
    endif()
elseif(APPLE)
    # macOS: Different linking based on wrapper library type
    if(BUILD_WRAPPER_SHARED)
        # Shared library: Use interface library
        target_link_libraries(embedded-ruby PRIVATE
                core
                $<$<BOOL:${BUILD_JNI}>:jni>
        )
    else()
        # Static library: Use -force_load to include all symbols
        target_link_libraries(embedded-ruby PRIVATE
                -Wl,-force_load,$<TARGET_FILE:logging>
                -Wl,-force_load,$<TARGET_FILE:ruby-vm>
                $<$<BOOL:${BUILD_JNI}>:jni>
        )
    endif()
else()
    # Other platforms (Windows, etc.)
    target_link_libraries(embedded-ruby PRIVATE
            core
            $<$<BOOL:${BUILD_JNI}>:jni>
    )
endif()

# This ensures the embedded dependency list is always up-to-date
# Also creates a standalone deps.txt file that can be read without loading the library
# Note: Only extract dependencies for shared wrapper libraries (static libs have no NEEDED entries)
if(BUILD_WRAPPER_SHARED)
    add_custom_command(TARGET embedded-ruby POST_BUILD
        COMMAND bash -c "${CMAKE_SOURCE_DIR}/core/ruby-vm/extract_deps.sh $<TARGET_FILE:embedded-ruby> ${EXTERNAL_LIBS_DIR} ${CMAKE_BINARY_DIR}/deps_list.txt || true"
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_BINARY_DIR}/deps_list.txt ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/libembedded-ruby.deps
        COMMENT "Extracting library dependencies from libembedded-ruby.so"
    )
else()
    # For static wrapper builds, create a placeholder deps file
    file(WRITE ${CMAKE_BINARY_DIR}/create_static_deps.cmake
        "file(WRITE \"${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/libembedded-ruby.deps\" \"# Static build - no runtime dependencies\\n\")"
    )
    add_custom_command(TARGET embedded-ruby POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_BINARY_DIR}/create_static_deps.cmake
        COMMENT "Creating placeholder deps file for static build"
    )
endif()

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()


