cmake_minimum_required(VERSION 3.19.2)

project(assets C ASM)

# Include FetchContent module
include(FetchContent)

# Your binary embedding logic
include(${CMAKE_CURRENT_SOURCE_DIR}/embed_binary.cmake)

set(RUBY_STDLIB_ZIP_PATH  "ruby-stdlib.zip")
embed_binary("${CMAKE_CURRENT_SOURCE_DIR}/files" ${RUBY_STDLIB_ZIP_PATH} EMBEDDED_RUBY_STDLIB)

set(RUBY_STDLIB_EXT_ZIP_PATH  "ruby-stdlib-ext.zip")
embed_binary("${CMAKE_CURRENT_SOURCE_DIR}/files/${HOST}" ${RUBY_STDLIB_EXT_ZIP_PATH} EMBEDDED_RUBY_STDLIB_EXT)

set(FIFO_INTERPRETER_RB_PATH "fifo_interpreter.rb")
embed_binary("${CMAKE_CURRENT_SOURCE_DIR}/files" ${FIFO_INTERPRETER_RB_PATH} EMBEDDED_FIFO_INTERPRETER)

# Fetch minizip-ng
FetchContent_Declare(
        minizip-ng
        GIT_REPOSITORY https://github.com/zlib-ng/minizip-ng.git
        GIT_TAG 4.0.4
        GIT_SHALLOW TRUE  # Only fetch the specific tag, not full history
)

# Configure minizip-ng options before making available
set(MZ_BUILD_TESTS OFF CACHE BOOL "Build minizip tests")
set(MZ_BUILD_UNIT_TESTS OFF CACHE BOOL "Build minizip unit tests")
set(MZ_COMPAT ON CACHE BOOL "Enable minizip 1.x compatibility layer")

FetchContent_MakeAvailable(minizip-ng)

set_target_properties(minizip PROPERTIES 
    POSITION_INDEPENDENT_CODE ON
)

add_library(assets STATIC
        ${EMBEDDED_FIFO_INTERPRETER}
        ${EMBEDDED_RUBY_STDLIB}
        ${EMBEDDED_RUBY_STDLIB_EXT}
        install.c
        use_direct_memory.c
)
set_target_properties(assets PROPERTIES COMPILE_FLAGS "-Wall -Wextra -Werror")

# Link minizip-ng to your assets library
target_link_libraries(assets PRIVATE minizip)

target_compile_definitions(assets PRIVATE HAS_EMBEDDED_DATA)

# For clarity: explicitly specify linker language if needed
set_target_properties(assets PROPERTIES LINKER_LANGUAGE C)

# Make minizip headers available to consumers of assets library
target_include_directories(assets PRIVATE
        $<TARGET_PROPERTY:minizip,INTERFACE_INCLUDE_DIRECTORIES>
)
