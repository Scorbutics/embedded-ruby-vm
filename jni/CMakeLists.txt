cmake_minimum_required(VERSION 3.19.2)

project("jni" C)

# Check for JAVA_HOME and find JNI headers
# When building for Android, the NDK provides JNI headers automatically
if(ANDROID)
    message(STATUS "Building for Android - using NDK JNI headers")
    # Android NDK provides jni.h in sysroot, no need to find JDK
    set(JNI_INCLUDE_DIRS "")
else()
    if(NOT DEFINED ENV{JAVA_HOME})
        message(FATAL_ERROR "JAVA_HOME environment variable is not set. Please set JAVA_HOME to your JDK installation directory.")
    endif()

    message(STATUS "JAVA_HOME: $ENV{JAVA_HOME}")

    # Find JNI package (includes jni.h and platform-specific headers)
    find_package(JNI REQUIRED)

    if(NOT JNI_FOUND)
        message(FATAL_ERROR "JNI headers not found. Please ensure JAVA_HOME points to a valid JDK installation with include/jni.h")
    endif()

    message(STATUS "JNI include directories: ${JNI_INCLUDE_DIRS}")
endif()

if(BUILD_SHARED_LIBS)
    find_library(zlib-lib z)

    if(NOT zlib-lib)
        message(FATAL_ERROR "The 'z' library (zlib) was not found. Please specify its location.")
    endif()
else()
    set(zlib-lib z)
endif()

# Build Android logging module if on Android
if(BUILD_JNI_ANDROID_LOG)
    add_subdirectory(android)
endif()

add_library(jni OBJECT
    ruby_vm_jni.c
    jni_ruby_info.c
    jni_logging.c
)

set_target_properties(jni PROPERTIES
        COMPILE_FLAGS "-Wall -Wextra -Werror"
        POSITION_INDEPENDENT_CODE ON
)

target_include_directories(jni PRIVATE
    ${EMBEDDED_RUBY_VM_MAIN_INCLUDE_DIR}/public
    ${EMBEDDED_RUBY_VM_MAIN_INCLUDE_DIR}/private
)

# Add JNI include directories only for non-Android builds
if(NOT ANDROID)
    target_include_directories(jni PRIVATE ${JNI_INCLUDE_DIRS})
endif()

target_link_libraries(jni
    core
    # zlib is either found in current build env, or provided by external libs
    ${zlib-lib}
)

# Link Android logging module if available
if(BUILD_JNI_ANDROID_LOG AND TARGET jni_logging_android)
    target_link_libraries(jni jni_logging_android)
    message(STATUS "Linked Android logging module to jni target")
endif()