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

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}-${CMAKE_CXX_PLATFORM_ID}-${CMAKE_SYSTEM_NAME}" HOST)
message(NOTICE "Configuring for host: ${HOST}")

set(EXTERNAL_LIBS_DIR ${CMAKE_SOURCE_DIR}/external/lib/${HOST})

option(BUILD_JNI "Build JNI binding" OFF)
option(BUILD_JNI_ANDROID_LOG "Build Android Logcat output" OFF)
option(BUILD_TESTS "Build tests" ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

add_subdirectory(core)
add_subdirectory(assets)

if(BUILD_JNI)
    set(JNI_DIR ${CMAKE_SOURCE_DIR}/jni)
    add_subdirectory(jni)
endif()

add_library(embedded-ruby SHARED dummy.c)

# Use whole-archive to export all symbols from static libraries into the shared library
# This is needed for Kotlin/Native dlopen() approach
if(UNIX AND NOT APPLE)
    target_link_libraries(embedded-ruby PRIVATE
            -Wl,--whole-archive
            logging
            ruby-vm
            -Wl,--no-whole-archive
            $<$<BOOL:${BUILD_JNI}>:jni>
    )
else()
    target_link_libraries(embedded-ruby PRIVATE
            core
            $<$<BOOL:${BUILD_JNI}>:jni>
    )
endif()

# Post-build: Extract dependencies and regenerate library-dependencies.c
# This ensures the embedded dependency list is always up-to-date
# Also creates a standalone deps.txt file that can be read without loading the library
add_custom_command(TARGET embedded-ruby POST_BUILD
    COMMAND bash -c "${CMAKE_SOURCE_DIR}/core/ruby-vm/extract_deps.sh $<TARGET_FILE:embedded-ruby> ${EXTERNAL_LIBS_DIR} ${CMAKE_BINARY_DIR}/deps_list.txt || true"
    COMMAND ${CMAKE_COMMAND}
        -DDEPS_LIST_FILE=${CMAKE_BINARY_DIR}/deps_list.txt
        -DDEPS_C_IN=${CMAKE_SOURCE_DIR}/core/ruby-vm/library-dependencies.c.in
        -DDEPS_C_OUT=${CMAKE_BINARY_DIR}/core/ruby-vm/library-dependencies.c
        -P ${CMAKE_SOURCE_DIR}/core/ruby-vm/update_deps.cmake
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_BINARY_DIR}/deps_list.txt ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/libembedded-ruby.deps
    COMMENT "Extracting library dependencies from libembedded-ruby.so"
)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()


