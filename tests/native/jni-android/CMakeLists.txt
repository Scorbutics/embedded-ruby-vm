cmake_minimum_required(VERSION 3.19.2)

project("test-jni-android")

# JNI Android logging tests - tests that Android logging module is properly linked
# This requires BUILD_JNI_ANDROID_LOG=ON which builds the jni_logging_android target

if(NOT TARGET jni_logging_android)
    message(FATAL_ERROR "jni_logging_android target not found. Build with BUILD_JNI_ANDROID_LOG=ON")
endif()

add_executable(test_jni_android_log test_jni_android_log.c)

# Link with jni_logging (weak symbol) and jni_logging_android (strong symbol override)
target_link_libraries(test_jni_android_log
    ${CMAKE_SOURCE_DIR}/jni/jni_logging.c
    jni_logging_android
)

# Include jni directory for headers
target_include_directories(test_jni_android_log PRIVATE
    ${CMAKE_SOURCE_DIR}/jni
)

# Register with CTest
add_test(NAME test_jni_android_log COMMAND test_jni_android_log)
