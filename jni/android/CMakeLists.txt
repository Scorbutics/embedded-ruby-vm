cmake_minimum_required(VERSION 3.19.2)

project("jni-android-logging" C)

# Find Android log library
find_library(log-lib log)

if(NOT log-lib)
    message(FATAL_ERROR "The 'log' library (Android NDK) was not found. This module requires Android.")
endif()

# Create static library with Android-specific implementations
add_library(jni_logging_android STATIC
    android_logging.c
    get_activity_parameters.c
)

target_include_directories(jni_logging_android PUBLIC ..)

target_link_libraries(jni_logging_android PUBLIC ${log-lib})

set_target_properties(jni_logging_android PROPERTIES
    COMPILE_FLAGS "-Wall -Wextra -Werror"
    POSITION_INDEPENDENT_CODE ON
)
