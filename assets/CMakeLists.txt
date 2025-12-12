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

set(RUBY_STDLIB_ZIP_PATH  "ruby-stdlib.zip")
embed_binary("${CMAKE_CURRENT_SOURCE_DIR}/files" ${RUBY_STDLIB_ZIP_PATH} EMBEDDED_RUBY_STDLIB)

set(RUBY_STDLIB_EXT_ZIP_PATH  "ruby-stdlib-ext.zip")
embed_binary("${CMAKE_CURRENT_SOURCE_DIR}/files/${HOST}" ${RUBY_STDLIB_EXT_ZIP_PATH} EMBEDDED_RUBY_STDLIB_EXT)

set(FIFO_INTERPRETER_RB_PATH "fifo_interpreter.rb")
embed_binary("${CMAKE_CURRENT_SOURCE_DIR}/files" ${FIFO_INTERPRETER_RB_PATH} EMBEDDED_FIFO_INTERPRETER)

# Embed platform-specific native libraries (Phase 2)
# Only embed libraries matching the current build platform to avoid binary bloat
set(NATIVE_LIBS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../external/lib/${HOST}")

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
        ${EMBEDDED_FIFO_INTERPRETER}
        ${EMBEDDED_RUBY_STDLIB}
        ${EMBEDDED_RUBY_STDLIB_EXT}
        ${EMBEDDED_NATIVE_LIBS_LIST}
        assets-error.c
        install.c
        use_direct_memory.c
)

# Add JNI bindings if available
if(ENABLE_ASSETS_JNI)
    list(APPEND ASSETS_SOURCES assets-jni.c)
    message(STATUS "Including JNI bindings in assets library")
endif()

# Build static library (for direct C linking without LD_LIBRARY_PATH hassles)
add_library(assets_static STATIC ${ASSETS_SOURCES})

set_target_properties(assets_static PROPERTIES
    COMPILE_FLAGS "-Wall -Wextra -Werror"
    OUTPUT_NAME "assets"  # Output will be libassets.a
)

target_link_libraries(assets_static PRIVATE minizip)

if(ENABLE_ASSETS_JNI)
    target_include_directories(assets_static PRIVATE ${JNI_INCLUDE_DIRS})
endif()

target_compile_definitions(assets_static PRIVATE HAS_EMBEDDED_DATA)

if(EMBEDDED_NATIVE_LIBS_LIST)
    target_compile_definitions(assets_static PRIVATE HAS_EMBEDDED_NATIVE_LIBS)
    target_compile_definitions(assets_static PRIVATE EMBEDDED_PLATFORM="${HOST}")
endif()

set_target_properties(assets_static PROPERTIES LINKER_LANGUAGE C)

target_include_directories(assets_static PRIVATE
        $<TARGET_PROPERTY:minizip,INTERFACE_INCLUDE_DIRECTORIES>
        ${CMAKE_CURRENT_SOURCE_DIR}/../ruby-vm
)

target_include_directories(assets_static PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Build shared library (for JNI/Kotlin usage)
add_library(assets SHARED ${ASSETS_SOURCES})

set_target_properties(assets PROPERTIES
    COMPILE_FLAGS "-Wall -Wextra -Werror"
    OUTPUT_NAME "assets"  # Output will be libassets.so
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

set_target_properties(assets PROPERTIES LINKER_LANGUAGE C)

target_include_directories(assets PRIVATE
        $<TARGET_PROPERTY:minizip,INTERFACE_INCLUDE_DIRECTORIES>
        ${CMAKE_CURRENT_SOURCE_DIR}/../ruby-vm
)

target_include_directories(assets PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
