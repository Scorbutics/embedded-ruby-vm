# Physfs.cmake
# Builds PhysicsFS (libphysfs.a) as a sub-build so the ruby-physfs gem can
# link against it. PhysFS is consumed *only* by ruby-physfs in this project,
# which is why it lives here in embedded-ruby-vm rather than in ruby-for-android
# (which ports the CRuby interpreter, not gems).
#
# Produces:
#   - IMPORTED static library target `physfs` (libphysfs.a in ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
#   - Header staged at ${PHYSFS_INSTALL_DIR}/include/physfs.h (used by ruby-physfs build)
#
# Cross-compile settings inherit from the parent build via get_sub_cmake_args().

include(ExternalProject)
include("${CMAKE_CURRENT_LIST_DIR}/SubCMakeArgs.cmake")

set(PHYSFS_VERSION "3.2.0")
set(PHYSFS_URL "https://github.com/icculus/physfs/archive/refs/tags/release-${PHYSFS_VERSION}.tar.gz")
set(PHYSFS_HASH "SHA256=1991500eaeb8d5325e3a8361847ff3bf8e03ec89252b7915e1f25b3f8ab5d560")

set(PHYSFS_PREFIX_DIR  "${CMAKE_BINARY_DIR}/physfs")
set(PHYSFS_INSTALL_DIR "${PHYSFS_PREFIX_DIR}/install")
set(PHYSFS_LIB_OUTPUT  "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/libphysfs.a")

get_sub_cmake_args(_PHYSFS_CMAKE_ARGS)

ExternalProject_Add(physfs_ep
    URL                          ${PHYSFS_URL}
    URL_HASH                     ${PHYSFS_HASH}
    PREFIX                       ${PHYSFS_PREFIX_DIR}
    DOWNLOAD_NAME                "physfs-${PHYSFS_VERSION}.tar.gz"
    DOWNLOAD_EXTRACT_TIMESTAMP   FALSE
    CMAKE_ARGS
        ${_PHYSFS_CMAKE_ARGS}
        # PhysFS 3.2.0's cmake_minimum_required is below 3.5, which CMake 4.x
        # rejects outright. Allow it.
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5
        -DPHYSFS_BUILD_STATIC=ON
        -DPHYSFS_BUILD_SHARED=OFF
        -DPHYSFS_BUILD_TEST=OFF
        -DPHYSFS_BUILD_DOCS=OFF
        -DPHYSFS_DISABLE_INSTALL=OFF
        -DCMAKE_INSTALL_PREFIX=${PHYSFS_INSTALL_DIR}
        -DCMAKE_INSTALL_LIBDIR=lib
        -DCMAKE_INSTALL_INCLUDEDIR=include
    BUILD_BYPRODUCTS ${PHYSFS_INSTALL_DIR}/lib/libphysfs.a ${PHYSFS_LIB_OUTPUT}
    # After install, copy the .a into ${CMAKE_LIBRARY_OUTPUT_DIRECTORY} so that
    # gradle's copy-to-libs step picks it up alongside libembedded-ruby.a.
    INSTALL_COMMAND  ${CMAKE_COMMAND} --install <BINARY_DIR>
            COMMAND  ${CMAKE_COMMAND} -E make_directory ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
            COMMAND  ${CMAKE_COMMAND} -E copy_if_different
                       ${PHYSFS_INSTALL_DIR}/lib/libphysfs.a
                       ${PHYSFS_LIB_OUTPUT}
)

# IMPORTED target so consumers (ruby-vm) can `target_link_libraries(... physfs)`
# exactly as before, regardless of where the .a came from.
#
# CMake requires every path listed in INTERFACE_INCLUDE_DIRECTORIES to exist
# at configure time, but the install step (which populates include/) doesn't
# run until build time. Pre-create the dir so the imported target validates.
file(MAKE_DIRECTORY ${PHYSFS_INSTALL_DIR}/include)
add_library(physfs STATIC IMPORTED GLOBAL)
set_target_properties(physfs PROPERTIES
    IMPORTED_LOCATION ${PHYSFS_LIB_OUTPUT}
    INTERFACE_INCLUDE_DIRECTORIES ${PHYSFS_INSTALL_DIR}/include
)
add_dependencies(physfs physfs_ep)

message(STATUS "physfs: building from ${PHYSFS_URL} -> ${PHYSFS_LIB_OUTPUT}")
