cmake_minimum_required(VERSION 3.19.2)

project("test-core")

# Core-level tests - tests Ruby VM functionality with fully static configuration
# This test ALWAYS builds with BUILD_WRAPPER_SHARED=OFF and BUILD_SHARED_LIBS=OFF
# to ensure a fully static binary that doesn't depend on any shared Ruby libraries.

add_executable(test_core
    test_core.c
)

# Regression: sentinel must reach the C side even when VMLogger log level is
# raised to ERROR (NDEBUG=1 / RUBY_VM_LOG_LEVEL=2). Otherwise every script
# burns the full logging_wait_for_sentinel timeout.
add_executable(test_sentinel_log_level
    test_sentinel_log_level.c
)

# Async dispatch queue: exercise basic dispatch, bounded drop under load,
# and recursive-printing-callback no-deadlock (the case the previous dup2
# swap-based design used to "protect" against by silently losing script
# writes during the swap window).
add_executable(test_dispatch_queue
    test_dispatch_queue.c
)

# Remote DAP debugger: verifies the Ruby `debug` gem's TCP listener comes up
# when ruby_interpreter_enable_remote_debug is called, that an empty token is
# refused at the API surface, and that no listener exists without the call.
add_executable(test_remote_debug
    test_remote_debug.c
)

# Link against assets library (static or shared depending on BUILD_SHARED_LIBS)
# The ruby-api-loader handles dlopen/dlsym for libembedded-ruby.so
# Link against embedded-ruby which combines logging, ruby-vm, and jni OBJECT libraries
target_link_libraries(test_core
    assets          # For assets_bootstrap() - handles extraction and library loading
    embedded-ruby   # For ruby-api-loader.h and all Ruby VM functionality
    dl              # For dlopen/dlsym in ruby-api-loader
)

target_link_libraries(test_sentinel_log_level
    assets
    embedded-ruby
    dl
)

target_link_libraries(test_dispatch_queue
    assets
    embedded-ruby
    dl
)

target_link_libraries(test_remote_debug
    assets
    embedded-ruby
    dl
)

# For static builds, ensure libcrypt's dependency on libbsd is satisfied
# libcrypt requires arc4random_buf from libbsd, which requires libmd
# These are already linked via embedded-ruby's dependencies (from ruby-vm), but we need them AFTER -lcrypt
# Use link options to append them at the end of the link line
if(NOT BUILD_SHARED_LIBS AND UNIX AND NOT APPLE)
    # Determine the correct library directory based on HOST
    if(BUILD_SHARED_LIBS)
        set(TEST_BUILD_TYPE_DIR "shared")
    else()
        set(TEST_BUILD_TYPE_DIR "static")
    endif()
    set(TEST_EXTERNAL_LIBS_DIR ${CMAKE_SOURCE_DIR}/external/lib/${HOST}/${TEST_BUILD_TYPE_DIR})

    # Find the libraries using the correct HOST architecture
    find_library(BSD_LIB_TEST NAMES bsd PATHS ${TEST_EXTERNAL_LIBS_DIR} NO_DEFAULT_PATH)
    find_library(MD_LIB_TEST NAMES md PATHS ${TEST_EXTERNAL_LIBS_DIR} NO_DEFAULT_PATH)
    
    if(BSD_LIB_TEST AND MD_LIB_TEST)
        message(STATUS "Appending libbsd and libmd to end of link line for test_core")
        # Use target_link_options to append libraries at the very end
        # This ensures they come AFTER -lcrypt in the link order
        target_link_options(test_core PRIVATE
            "${BSD_LIB_TEST}"
            "${MD_LIB_TEST}"
        )
        target_link_options(test_sentinel_log_level PRIVATE
            "${BSD_LIB_TEST}"
            "${MD_LIB_TEST}"
        )
        target_link_options(test_dispatch_queue PRIVATE
            "${BSD_LIB_TEST}"
            "${MD_LIB_TEST}"
        )
        target_link_options(test_remote_debug PRIVATE
            "${BSD_LIB_TEST}"
            "${MD_LIB_TEST}"
        )
    endif()
endif()

# Include directories for headers
target_include_directories(test_core PRIVATE
    ${CMAKE_SOURCE_DIR}/include/private
    ${CMAKE_SOURCE_DIR}/include/public
)
target_include_directories(test_sentinel_log_level PRIVATE
    ${CMAKE_SOURCE_DIR}/include/private
    ${CMAKE_SOURCE_DIR}/include/public
)
target_include_directories(test_dispatch_queue PRIVATE
    ${CMAKE_SOURCE_DIR}/include/private
    ${CMAKE_SOURCE_DIR}/include/public
)
target_include_directories(test_remote_debug PRIVATE
    ${CMAKE_SOURCE_DIR}/include/private
    ${CMAKE_SOURCE_DIR}/include/public
)

# Set RPATH so the executable can find libembedded-ruby.so at runtime
if(UNIX)
    set_target_properties(test_core PROPERTIES
        BUILD_RPATH "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
        INSTALL_RPATH "$ORIGIN/../lib"
    )
    set_target_properties(test_sentinel_log_level PROPERTIES
        BUILD_RPATH "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
        INSTALL_RPATH "$ORIGIN/../lib"
    )
    set_target_properties(test_dispatch_queue PROPERTIES
        BUILD_RPATH "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
        INSTALL_RPATH "$ORIGIN/../lib"
    )
    set_target_properties(test_remote_debug PROPERTIES
        BUILD_RPATH "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
        INSTALL_RPATH "$ORIGIN/../lib"
    )
endif()

# Note: No RPATH needed since we're using static linking for libassets.a
# The executable can be run directly without LD_LIBRARY_PATH for the assets library

# Register with CTest
add_test(NAME test_core COMMAND test_core WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
add_test(NAME test_sentinel_log_level COMMAND test_sentinel_log_level WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
add_test(NAME test_dispatch_queue COMMAND test_dispatch_queue WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
add_test(NAME test_remote_debug COMMAND test_remote_debug WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
