cmake_minimum_required(VERSION 3.19.2)

project("logging" C)

add_library(logging OBJECT logging.c jni_logging_fallback.c)
set_target_properties(logging PROPERTIES
    COMPILE_FLAGS "-Wall -Wextra -Werror"
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(logging 
    PUBLIC
    ${EMBEDDED_RUBY_VM_MAIN_INCLUDE_DIR}/public
    PRIVATE
    ${EMBEDDED_RUBY_VM_MAIN_INCLUDE_DIR}/private
)
