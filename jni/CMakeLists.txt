cmake_minimum_required(VERSION 3.19.2)

project("jni" C)

find_library(log-lib log)
find_library(zlib-lib z)

if(NOT zlib-lib)
    message(FATAL_ERROR "The 'z' library (zlib) was not found. Please specify its location.")
endif()

if(NOT log-lib)
    message(FATAL_ERROR "The 'log' library, part of Android NDK, was not found. Please specify its location.")
endif()

add_library(jni OBJECT ruby_vm_jni.c jni_ruby_info.c get_activity_parameters.c)
set_target_properties(jni PROPERTIES 
        COMPILE_FLAGS "-Wall -Wextra -Werror"
        POSITION_INDEPENDENT_CODE ON
)
target_link_libraries(jni 
        core
        ${log-lib} 
        ${zlib-lib}
)