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

    find_library(GMP_LIBRARY NAMES gmp)
    if(NOT GMP_LIBRARY)
        message(WARNING "GMP library not found. Static Ruby builds require:\n"
                        "  - Debian/Ubuntu (glibc): apt-get install libgmp-dev\n"
                        "  - Alpine (musl): apk add gmp-dev")
    else()
        message(STATUS "GMP library found: ${GMP_LIBRARY}")
    endif()

    # BSD library is only needed on glibc systems (not needed on musl/Alpine)
    # On musl, these functions are built into libc
    find_library(BSD_LIBRARY NAMES bsd)
    if(BSD_LIBRARY)
        message(STATUS "BSD library found: ${BSD_LIBRARY}")
    else()
        message(WARNING "BSD library not found (OK on musl/Alpine systems)")
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
        # - gmp: GNU Multiple Precision (big integers)
        # - dl: dynamic loading
        # - pthread: POSIX threads
        # - crypt: password hashing
        set(RUBY_SYSTEM_LIBS "m;gmp;dl;pthread;crypt")

        # BSD library is only needed on glibc systems (provides issetugid, __flt_rounds)
        # On musl (Alpine), these are built into libc
        if(BSD_LIBRARY)
            list(APPEND RUBY_SYSTEM_LIBS "bsd")
        endif()

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
    ${RUBY_VM_DIR}
    ${LOGGING_DIR}
    ${IMPORT_DIR}
)
