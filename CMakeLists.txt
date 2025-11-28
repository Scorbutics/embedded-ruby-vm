cmake_minimum_required(VERSION 3.19.2)

project("embedded-ruby-vm")

# Enable Position Independent Code for all targets
# Required when linking static libraries into shared libraries
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}-${CMAKE_CXX_PLATFORM_ID}-${CMAKE_SYSTEM_NAME}" HOST)
message(NOTICE "Configuring for host: ${HOST}")

option(BUILD_JNI "Build JNI binding" OFF)
option(BUILD_JNI_ANDROID_LOG "Build Android Logcat output" OFF)
option(BUILD_TESTS "Build tests" ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

add_subdirectory(core)

if(BUILD_JNI)
    set(JNI_DIR ${CMAKE_SOURCE_DIR}/jni)
    add_subdirectory(jni)
endif()

add_library(embedded-ruby SHARED dummy.c)

target_link_libraries(embedded-ruby PRIVATE
        core
        $<$<BOOL:${BUILD_JNI}>:jni>
)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()


