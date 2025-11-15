cmake_minimum_required(VERSION 3.19.2)

project("ruby-vm" C)

add_library(ruby-vm OBJECT ruby-comm-channel.c env.c exec-main-vm.c ruby-interpreter.c ruby-script.c ruby-script-location.c ruby-vm.c)
set_target_properties(ruby-vm PROPERTIES 
    COMPILE_FLAGS "-Wall -Wextra -Werror"
    POSITION_INDEPENDENT_CODE ON
)
target_include_directories(ruby-vm PRIVATE ${IMPORT_DIR} ${LOGGING_DIR} ${ASSETS_DIR})
target_link_libraries(ruby-vm logging assets ruby)
