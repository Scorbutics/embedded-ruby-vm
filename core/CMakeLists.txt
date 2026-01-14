cmake_minimum_required(VERSION 3.19.2)

project("core")

# Set build type directory for include/lib resolution
if(BUILD_SHARED_LIBS)
    set(BUILD_TYPE_DIR "shared")
else()
    set(BUILD_TYPE_DIR "static")
endif()

set(IMPORT_DIR
    ${CMAKE_SOURCE_DIR}/external/include
    ${CMAKE_SOURCE_DIR}/external/include/${HOST}/${BUILD_TYPE_DIR}
)
set(LOGGING_DIR ${CMAKE_CURRENT_SOURCE_DIR}/logging)
set(RUBY_VM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ruby-vm)
set(EXTERNAL_LIBS_DIR ${CMAKE_SOURCE_DIR}/external/lib/${HOST}/${BUILD_TYPE_DIR})

# For static builds, find required system libraries that Ruby depends on
if(NOT BUILD_SHARED_LIBS)
    # Check if the static Ruby was built with musl
    execute_process(
        COMMAND readelf -p .comment ${EXTERNAL_LIBS_DIR}/libruby-static.a
        OUTPUT_VARIABLE RUBY_BUILD_INFO
        ERROR_QUIET
    )

    if(RUBY_BUILD_INFO MATCHES "musl")
        message(WARNING
            "Detected musl-based static Ruby library, but building with glibc.\n"
            "This will cause linking errors. Solutions:\n"
            "  1. Build with musl toolchain: CC=musl-gcc cmake -DBUILD_SHARED_LIBS=OFF ..\n"
            "  2. Use glibc-based Ruby static library\n"
            "  3. Use shared libraries instead: cmake -DBUILD_SHARED_LIBS=ON .."
        )
    endif()

    # Find libgmp, libbsd, libmd, and libcrypt using the correct HOST architecture
    # These are only needed on glibc systems (not needed on musl/Alpine or Android Bionic)
    # On musl, these functions are built into libc
    # On Android, Bionic provides these functions
    # CMAKE_SYSTEM_NAME is "Linux" and CMAKE_C_LIBRARY_ARCHITECTURE contains "gnu" for glibc systems
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ANDROID)
        # Check if we're using GNU libc (glibc) by testing for __GLIBC__
        include(CheckSymbolExists)
        check_symbol_exists(__GLIBC__ "features.h" HAVE_GLIBC)

        if(HAVE_GLIBC OR CMAKE_C_LIBRARY_ARCHITECTURE MATCHES "gnu")
            find_library(GMP_LIBRARY NAMES gmp PATHS ${EXTERNAL_LIBS_DIR} NO_DEFAULT_PATH)
            find_library(BSD_LIBRARY NAMES bsd PATHS ${EXTERNAL_LIBS_DIR} NO_DEFAULT_PATH)
            find_library(MD_LIBRARY NAMES md PATHS ${EXTERNAL_LIBS_DIR} NO_DEFAULT_PATH)
            find_library(CRYPT_LIBRARY NAMES crypt PATHS ${EXTERNAL_LIBS_DIR} NO_DEFAULT_PATH)

            if(GMP_LIBRARY)
                message(STATUS "GMP library found: ${GMP_LIBRARY}")
            else()
                message(WARNING "libgmp not found - will use system libgmp")
            endif()

            if(BSD_LIBRARY)
                message(STATUS "Found libbsd: ${BSD_LIBRARY}")
            else()
                message(WARNING "libbsd not found (OK on musl/Alpine systems)")
            endif()

            if(MD_LIBRARY)
                message(STATUS "Found libmd: ${MD_LIBRARY}")
            else()
                message(WARNING "libmd not found (OK on musl/Alpine systems)")
            endif()

            if(CRYPT_LIBRARY)
                message(STATUS "Found libcrypt: ${CRYPT_LIBRARY}")
            else()
                message(WARNING "libcrypt not found - will use system libcrypt")
            endif()
        else()
            message(STATUS "Non-glibc Linux detected (musl/Alpine) - skipping glibc-specific library search")
        endif()
    elseif(ANDROID)
        message(STATUS "Android detected - skipping glibc-specific library search (Bionic libc provides these functions)")
    endif()
endif()

if(BUILD_SHARED_LIBS)
    set(libs_dependencies ruby)
else()
    # Static Native ruby extensions are packaged here
    set(libs_dependencies ruby-ext ruby)
endif()

list(APPEND libs_dependencies
    crypto 
    form 
    gdbm_compat 
    gdbm 
    history
    menu
    ncurses
    ncurses++
    panel
    readline
    ssl
    z
)

# Determine library extension based on BUILD_SHARED_LIBS and platform
if(BUILD_SHARED_LIBS)
    if(APPLE)
        set(LIB_EXT "dylib")
    else()
        set(LIB_EXT "so")
    endif()
else()
    set(LIB_EXT "a")
endif()

FOREACH(lib_name ${libs_dependencies})
    if(BUILD_SHARED_LIBS)
        # Shared library build
        add_library(${lib_name} SHARED IMPORTED)
        set_target_properties(${lib_name} PROPERTIES
            IMPORTED_LOCATION ${EXTERNAL_LIBS_DIR}/lib${lib_name}.${LIB_EXT}
        )
    else()
        # Static library build
        # Ruby provides libruby-static.a for static linking
        add_library(${lib_name} STATIC IMPORTED)

        # Build list of required system libraries
        # Base libraries needed by all static Ruby builds:
        # - m: math functions (sin, cos, sqrt, etc.)
        # - dl: dynamic loading
        # - pthread: POSIX threads
        set(RUBY_SYSTEM_LIBS "m;dl;pthread")

        # The following libraries are glibc-specific and not needed on Android/Bionic or musl
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ANDROID)
            if(HAVE_GLIBC OR CMAKE_C_LIBRARY_ARCHITECTURE MATCHES "gnu")
                # Only add system libcrypt if we don't have our own static version
                # This prevents duplicate libcrypt linkage (static + system)
                # - crypt: password hashing (only if we don't have our own static libcrypt.a)
                if(NOT CRYPT_LIBRARY)
                    list(APPEND RUBY_SYSTEM_LIBS "crypt")
                endif()

                # - gmp: GNU Multiple Precision (big integers)
                # Same for libgmp
                if(NOT GMP_LIBRARY)
                    find_library(GMP_SYSTEM_LIBRARY NAMES gmp)
                    if(GMP_SYSTEM_LIBRARY)
                        list(APPEND RUBY_SYSTEM_LIBS "gmp")
                    endif()
                endif()

                # BSD library is only needed on glibc systems (provides issetugid, __flt_rounds)
                # On musl (Alpine), these are built into libc
                # Only add if we don't have our own static version
                if(NOT BSD_LIBRARY)
                    # No static libbsd.a, try system library
                    find_library(BSD_SYSTEM_LIBRARY NAMES bsd)
                    if(BSD_SYSTEM_LIBRARY)
                        list(APPEND RUBY_SYSTEM_LIBS "bsd")
                    endif()
                endif()
            endif()
        endif()

        # TODO replace all of this with find_library
        if(EXISTS ${EXTERNAL_LIBS_DIR}/lib${lib_name}-static.a)
            set_target_properties(${lib_name} PROPERTIES
                IMPORTED_LOCATION ${EXTERNAL_LIBS_DIR}/lib${lib_name}-static.a
                INTERFACE_LINK_LIBRARIES "${RUBY_SYSTEM_LIBS}"
            )
        else()
            # Fallback to libruby.a if libruby-static.a doesn't exist
            set_target_properties(${lib_name} PROPERTIES
                IMPORTED_LOCATION ${EXTERNAL_LIBS_DIR}/lib${lib_name}.a
                INTERFACE_LINK_LIBRARIES "${RUBY_SYSTEM_LIBS}"
            )
        endif()
    endif()
ENDFOREACH()

add_subdirectory(logging)
add_subdirectory(ruby-vm)

add_library(core INTERFACE)

target_link_libraries(core INTERFACE
        logging
        ruby-vm
)
target_include_directories(core INTERFACE 
    ${EMBEDDED_RUBY_VM_MAIN_INCLUDE_DIR}/public
    ${EMBEDDED_RUBY_VM_MAIN_INCLUDE_DIR}/private
    ${IMPORT_DIR}
)
