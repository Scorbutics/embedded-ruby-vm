cmake_minimum_required(VERSION 3.19.2)

project("tests")

# Multi-level test architecture based on build configuration
# Tests are organized by dependency level:
#   - core/           : Core Ruby VM tests (always built)
#   - jni/            : JNI layer tests (built when BUILD_JNI=ON)
#   - jni-android/    : Android logging tests (built when BUILD_JNI_ANDROID_LOG=ON)

message(STATUS "=== Test Configuration ===")

# Level 1: Core tests (always built)
message(STATUS "Building core tests")
add_subdirectory(core)

# Level 2: JNI tests (requires BUILD_JNI=ON)
if(BUILD_JNI)
    message(STATUS "Building JNI tests (BUILD_JNI=ON)")
    add_subdirectory(jni)
else()
    message(STATUS "Skipping JNI tests (BUILD_JNI=OFF)")
endif()

# Level 3: JNI Android logging tests (requires BUILD_JNI_ANDROID_LOG=ON)
if(BUILD_JNI_ANDROID_LOG)
    message(STATUS "Building JNI Android logging tests (BUILD_JNI_ANDROID_LOG=ON)")
    add_subdirectory(jni-android)
else()
    message(STATUS "Skipping JNI Android logging tests (BUILD_JNI_ANDROID_LOG=OFF)")
endif()

message(STATUS "=== End Test Configuration ===")
