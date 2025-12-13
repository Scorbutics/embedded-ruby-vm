cmake_minimum_required(VERSION 3.19.2)

project("test-core")

# Core-level tests - tests Ruby VM functionality with two-library architecture
# This test demonstrates the proper loading sequence:
#   1. Link against libassets.a (static) - no LD_LIBRARY_PATH needed
#   2. Extract assets (which includes native Ruby libraries)
#   3. Dynamically load the extracted libraries
#   4. Create and use the Ruby interpreter

add_executable(test_core
    test_core.c
)

# Link against static assets library - simpler for testing, no runtime dependencies
# The ruby-api-loader handles dlopen/dlsym for libembedded-ruby.so
target_link_libraries(test_core
    assets_static   # For assets_bootstrap() - handles extraction and library loading
    dl              # For dlopen/dlsym in ruby-api-loader
)

# Include directories for headers
target_include_directories(test_core PRIVATE
    ${CMAKE_SOURCE_DIR}/core/assets
    ${CMAKE_SOURCE_DIR}/core/ruby-vm
    ${CMAKE_SOURCE_DIR}/core/logging
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
