cmake_minimum_required(VERSION 3.19.2)

project(assets C ASM)


# Include FetchContent module
include(FetchContent)

# Check for JAVA_HOME and find JNI headers (needed for JNI bindings)
if(NOT DEFINED ENV{JAVA_HOME})
    message(WARNING "JAVA_HOME not set. JNI bindings will not be built for assets library.")
    set(ENABLE_ASSETS_JNI OFF)
else()
    find_package(JNI)
    if(JNI_FOUND)
        message(STATUS "JNI found for assets library: ${JNI_INCLUDE_DIRS}")
        set(ENABLE_ASSETS_JNI ON)
    else()
        message(WARNING "JNI not found. Assets JNI bindings will not be built.")
        set(ENABLE_ASSETS_JNI OFF)
    endif()
endif()

# Your binary embedding logic
include(${CMAKE_CURRENT_SOURCE_DIR}/embed_binary.cmake)

# Ruby standard library (platform-independent)
# Note: The stdlib files are the SAME for static and dynamic builds
# Ruby's require mechanism handles static vs dynamic loading automatically
set(RUBY_STDLIB_ZIP_PATH  "ruby-stdlib.zip")
if(BUILD_SHARED_LIBS)
    set(RUBY_BUILD_TYPE_DIR "shared")
    message(STATUS "Using shared Ruby libraries")
else()
    set(RUBY_BUILD_TYPE_DIR "static")
    message(STATUS "Using static Ruby libraries")
endif()

embed_binary("${CMAKE_CURRENT_SOURCE_DIR}/files" ${RUBY_STDLIB_ZIP_PATH} EMBEDDED_RUBY_STDLIB)

# Select Ruby stdlib extension zip based on build type (shared vs static)
# This allows different Ruby builds with different native extension linking strategies
set(RUBY_STDLIB_EXT_ZIP_PATH  "ruby-stdlib-ext.zip")
message(STATUS "Using Ruby stdlib extensions from ${HOST}/${RUBY_BUILD_TYPE_DIR}/")

embed_binary("${CMAKE_CURRENT_SOURCE_DIR}/files/${HOST}/${RUBY_BUILD_TYPE_DIR}" ${RUBY_STDLIB_EXT_ZIP_PATH} EMBEDDED_RUBY_STDLIB_EXT)

# Embed platform-specific native libraries (Phase 2)
# Only embed libraries matching the current build platform to avoid binary bloat
# Path includes build type (shared/static) to support different linking strategies
set(NATIVE_LIBS_DIR "${CMAKE_SOURCE_DIR}/external/lib/${HOST}/${RUBY_BUILD_TYPE_DIR}")

if(BUILD_SHARED_LIBS)
    # Define the list of native libraries to embed (main .so files, not symlinks)
    set(NATIVE_LIBS_TO_EMBED
        libruby.so
        libssl.so
        libcrypto.so
        libreadline.so
        libhistory.so
        libgdbm.so
        libgdbm_compat.so
        libncurses.so
        libpanel.so
        libmenu.so
        libform.so
        libcurses.so
    )
endif()

# List to hold all embedded native library object files
set(EMBEDDED_NATIVE_LIBS_LIST "")

# Embed each native library if the directory exists
if(EXISTS "${NATIVE_LIBS_DIR}")
    message(STATUS "Embedding native libraries from: ${NATIVE_LIBS_DIR}")

    foreach(lib_name IN LISTS NATIVE_LIBS_TO_EMBED)
        set(lib_path "${NATIVE_LIBS_DIR}/${lib_name}")

        # Check if the library file exists
        if(EXISTS "${lib_path}")
            # Create a variable name for this library (replace . and - with _)
            string(REPLACE "." "_" lib_var_name "${lib_name}")
            string(REPLACE "-" "_" lib_var_name "${lib_var_name}")
            string(TOUPPER "${lib_var_name}" lib_var_name)
            set(lib_var_name "EMBEDDED_NATIVELIB_${lib_var_name}")

            # Embed the library
            embed_binary("${NATIVE_LIBS_DIR}" "${lib_name}" ${lib_var_name})

            # Add to the list of embedded objects
            list(APPEND EMBEDDED_NATIVE_LIBS_LIST ${${lib_var_name}})

            message(STATUS "  - Embedding: ${lib_name}")
        else()
            message(STATUS "  - Skipping (not found): ${lib_name}")
        endif()
    endforeach()

    message(STATUS "Embedded ${CMAKE_MATCH_COUNT} native libraries")
else()
    message(STATUS "Native libs directory not found: ${NATIVE_LIBS_DIR}")
    message(STATUS "Building without embedded native libraries")
endif()

# Fetch minizip-ng
FetchContent_Declare(
        minizip-ng
        GIT_REPOSITORY https://github.com/zlib-ng/minizip-ng.git
        GIT_TAG 4.0.4
        GIT_SHALLOW TRUE  # Only fetch the specific tag, not full history
)

# Configure minizip-ng options before making available
set(MZ_BUILD_TESTS OFF CACHE BOOL "Build minizip tests")
set(MZ_BUILD_UNIT_TESTS OFF CACHE BOOL "Build minizip unit tests")
set(MZ_COMPAT ON CACHE BOOL "Enable minizip 1.x compatibility layer")

# Disable getrandom to avoid GLIBC 2.25+ dependency (for Kotlin/Native compatibility)
set(MZ_FETCH_LIBS OFF CACHE BOOL "Disable external lib fetching")
set(MZ_PKCRYPT OFF CACHE BOOL "Disable PKWARE traditional encryption")
set(MZ_SIGNING OFF CACHE BOOL "Disable signing support")

FetchContent_MakeAvailable(minizip-ng)

set_target_properties(minizip PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

# Override HAVE_GETRANDOM and HAVE_ARC4RANDOM_BUF after minizip is configured
# This forces minizip to use /dev/urandom instead
target_compile_options(minizip PRIVATE -UHAVE_GETRANDOM -UHAVE_ARC4RANDOM_BUF)

# Build list of source files
set(ASSETS_SOURCES
        ${EMBEDDED_RUBY_STDLIB}
        ${EMBEDDED_RUBY_STDLIB_EXT}
        ${EMBEDDED_NATIVE_LIBS_LIST}
        assets-error.c
        install.c
)

# Add JNI bindings if available
if(ENABLE_ASSETS_JNI)
    list(APPEND ASSETS_SOURCES assets-jni.c)
    message(STATUS "Including JNI bindings in assets library")
endif()

# Build assets library - type determined by BUILD_SHARED_LIBS option
# When BUILD_SHARED_LIBS=ON: builds libassets.so (or .dylib on macOS)
# When BUILD_SHARED_LIBS=OFF: builds libassets.a (static library - default)
add_library(assets ${ASSETS_SOURCES})

set_target_properties(assets PROPERTIES
    COMPILE_FLAGS "-Wall -Wextra -Werror"
    OUTPUT_NAME "assets"
    LINKER_LANGUAGE C
)

target_link_libraries(assets PRIVATE minizip)

if(ENABLE_ASSETS_JNI)
    target_include_directories(assets PRIVATE ${JNI_INCLUDE_DIRS})
endif()

target_compile_definitions(assets PRIVATE HAS_EMBEDDED_DATA)

if(EMBEDDED_NATIVE_LIBS_LIST)
    target_compile_definitions(assets PRIVATE HAS_EMBEDDED_NATIVE_LIBS)
    target_compile_definitions(assets PRIVATE EMBEDDED_PLATFORM="${HOST}")
endif()

target_include_directories(assets PRIVATE
        $<TARGET_PROPERTY:minizip,INTERFACE_INCLUDE_DIRECTORIES>
        ${CMAKE_CURRENT_SOURCE_DIR}/../ruby-vm
)

target_include_directories(assets PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
