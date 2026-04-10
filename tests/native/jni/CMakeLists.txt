cmake_minimum_required(VERSION 3.19.2)

project("test-jni")

# JNI-level tests - tests JNI logging interface with custom implementation
# This test provides its own strong symbol jni_log_impl() to override the weak default

add_executable(test_jni
    test_jni.c
    ${CMAKE_SOURCE_DIR}/jni/jni_logging.c
)

# Include jni directory for headers
target_include_directories(test_jni PRIVATE
    ${CMAKE_SOURCE_DIR}/jni
    ${CMAKE_SOURCE_DIR}/include/private
)

# Register with CTest
add_test(NAME test_jni COMMAND test_jni)
