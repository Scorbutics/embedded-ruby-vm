# SubCMakeArgs.cmake
# Helper to forward cross-compile settings to a child CMake invocation
# (i.e. an ExternalProject_Add CMAKE_ARGS list) so the sub-build targets the
# same platform/arch as the parent.
#
# Output:
#   <OUT_VAR>  list variable receiving the args, set in PARENT_SCOPE

function(get_sub_cmake_args OUT_VAR)
    set(_args
        "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
        "-DCMAKE_POSITION_INDEPENDENT_CODE=ON"
    )

    if(CMAKE_TOOLCHAIN_FILE)
        list(APPEND _args "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
    endif()

    # Android: forward NDK / ABI / API level so the sub-build picks the same
    # cross-toolchain. android.toolchain.cmake is auto-picked by CMake when
    # CMAKE_SYSTEM_NAME=Android, so we don't need to set CMAKE_TOOLCHAIN_FILE
    # explicitly.
    if(CMAKE_SYSTEM_NAME)
        list(APPEND _args "-DCMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}")
    endif()
    if(CMAKE_SYSTEM_VERSION)
        list(APPEND _args "-DCMAKE_SYSTEM_VERSION=${CMAKE_SYSTEM_VERSION}")
    endif()
    if(CMAKE_ANDROID_NDK)
        list(APPEND _args "-DCMAKE_ANDROID_NDK=${CMAKE_ANDROID_NDK}")
    endif()
    if(CMAKE_ANDROID_ARCH_ABI)
        list(APPEND _args "-DCMAKE_ANDROID_ARCH_ABI=${CMAKE_ANDROID_ARCH_ABI}")
    endif()
    if(ANDROID_PLATFORM)
        list(APPEND _args "-DANDROID_PLATFORM=${ANDROID_PLATFORM}")
    endif()

    # Apple: forward sysroot / arch / deployment target.
    if(CMAKE_OSX_SYSROOT)
        list(APPEND _args "-DCMAKE_OSX_SYSROOT=${CMAKE_OSX_SYSROOT}")
    endif()
    if(CMAKE_OSX_ARCHITECTURES)
        list(APPEND _args "-DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}")
    endif()
    if(CMAKE_OSX_DEPLOYMENT_TARGET)
        list(APPEND _args "-DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}")
    endif()

    set(${OUT_VAR} "${_args}" PARENT_SCOPE)
endfunction()
