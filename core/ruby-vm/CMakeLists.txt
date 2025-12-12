cmake_minimum_required(VERSION 3.19.2)

project("ruby-vm" C)

# Include the script embedding function
include(${CMAKE_CURRENT_SOURCE_DIR}/embed_script.cmake)

# Embed fifo_interpreter.rb into the library
embed_script(fifo_interpreter.rb EMBEDDED_FIFO_INTERPRETER)

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
)

set_target_properties(ruby-vm PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    COMPILE_FLAGS "-Wall -Wextra -Werror"
    POSITION_INDEPENDENT_CODE ON
)
target_include_directories(ruby-vm PRIVATE ${IMPORT_DIR} ${LOGGING_DIR})
target_link_libraries(ruby-vm logging ruby)
