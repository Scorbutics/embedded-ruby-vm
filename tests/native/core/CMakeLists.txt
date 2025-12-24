cmake_minimum_required(VERSION 3.19.2)

project("test-core")

# Core-level tests - tests Ruby VM functionality with fully static configuration
# This test ALWAYS builds with BUILD_WRAPPER_SHARED=OFF and BUILD_SHARED_LIBS=OFF
# to ensure a fully static binary that doesn't depend on any shared Ruby libraries.

add_executable(test_core
    test_core.c
)

# Link against assets library (static or shared depending on BUILD_SHARED_LIBS)
# The ruby-api-loader handles dlopen/dlsym for libembedded-ruby.so
# Link against embedded-ruby which combines logging, ruby-vm, and jni OBJECT libraries
target_link_libraries(test_core
    assets          # For assets_bootstrap() - handles extraction and library loading
    embedded-ruby   # For ruby-api-loader.h and all Ruby VM functionality
    dl              # For dlopen/dlsym in ruby-api-loader
)

# For static builds, ensure libcrypt's dependency on libbsd is satisfied
# libcrypt requires arc4random_buf from libbsd, which requires libmd
# These are already linked via embedded-ruby's dependencies (from ruby-vm), but we need them AFTER -lcrypt
# Use link options to append them at the end of the link line
if(NOT BUILD_SHARED_LIBS AND UNIX AND NOT APPLE)
    # Find the libraries with full paths
    find_library(BSD_LIB_TEST NAMES bsd PATHS ${CMAKE_SOURCE_DIR}/external/lib/*/static NO_DEFAULT_PATH)
    find_library(MD_LIB_TEST NAMES md PATHS ${CMAKE_SOURCE_DIR}/external/lib/*/static NO_DEFAULT_PATH)
    
    if(BSD_LIB_TEST AND MD_LIB_TEST)
        message(STATUS "Appending libbsd and libmd to end of link line for test_core")
        # Use target_link_options to append libraries at the very end
        # This ensures they come AFTER -lcrypt in the link order
        target_link_options(test_core PRIVATE
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

# Set RPATH so the executable can find libembedded-ruby.so at runtime
if(UNIX)
    set_target_properties(test_core PROPERTIES
        BUILD_RPATH "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
        INSTALL_RPATH "$ORIGIN/../lib"
    )
endif()

# Note: No RPATH needed since we're using static linking for libassets.a
# The executable can be run directly without LD_LIBRARY_PATH for the assets library

# Register with CTest
add_test(NAME test_core COMMAND test_core WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
