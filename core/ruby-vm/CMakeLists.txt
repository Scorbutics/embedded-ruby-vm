cmake_minimum_required(VERSION 3.19.2)

project("ruby-vm" C)

# Include the script embedding function
include(${CMAKE_CURRENT_SOURCE_DIR}/embed_script.cmake)

# Embed fifo_interpreter.rb into the library
embed_script(fifo_interpreter.rb EMBEDDED_FIFO_INTERPRETER)

# Generate library dependencies file
set(DEPS_C_IN "${CMAKE_CURRENT_SOURCE_DIR}/library-dependencies.c.in")
set(DEPS_C_OUT "${CMAKE_CURRENT_BINARY_DIR}/library-dependencies.c")
set(DEPS_H "${CMAKE_CURRENT_SOURCE_DIR}/library-dependencies.h")

# This will be populated after embedded-ruby is built
set(DEPENDENCY_LIST "    /* Dependencies will be populated in embedded-ruby library */")

# Configure initial version
configure_file(${DEPS_C_IN} ${DEPS_C_OUT} @ONLY)

# Copy header to binary directory so the generated .c file can find it
configure_file(${DEPS_H} ${CMAKE_CURRENT_BINARY_DIR}/library-dependencies.h COPYONLY)

add_library(ruby-vm STATIC
    ruby-comm-channel.c
    env.c
    exec-main-vm.c
    ruby-interpreter.c
    ruby-script.c
    ruby-script-location.c
    ruby-vm.c
    ruby-vm-error.c
    embedded_scripts.c
    ${EMBEDDED_FIFO_INTERPRETER}
    ${DEPS_C_OUT}
)

set_target_properties(ruby-vm PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    COMPILE_FLAGS "-Wall -Wextra -Werror"
    POSITION_INDEPENDENT_CODE ON
)
target_include_directories(ruby-vm PRIVATE
    ${IMPORT_DIR}
    ${LOGGING_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}  # For generated library-dependencies.h
)
target_link_libraries(ruby-vm logging ruby)
