cmake_minimum_required(VERSION 3.19.2)

project("jni" C)

find_library(zlib-lib z)

if(NOT zlib-lib)
    message(FATAL_ERROR "The 'z' library (zlib) was not found. Please specify its location.")
endif()

# Build Android logging module if on Android
if(BUILD_JNI_ANDROID_LOG)
    add_subdirectory(android)
endif()

add_library(jni OBJECT
    ruby_vm_jni.c
    jni_ruby_info.c
    get_activity_parameters.c
    jni_logging.c
)

set_target_properties(jni PROPERTIES
        COMPILE_FLAGS "-Wall -Wextra -Werror"
        POSITION_INDEPENDENT_CODE ON
)

target_link_libraries(jni
        core
        ${zlib-lib}
)

# Link Android logging module if available
if(BUILD_JNI_ANDROID_LOG AND TARGET jni_logging_android)
    target_link_libraries(jni jni_logging_android)
    message(STATUS "Linked Android logging module to jni target")
endif()