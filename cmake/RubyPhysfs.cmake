# RubyPhysfs.cmake
# Configuration for the ruby-physfs gem (https://github.com/Scorbutics/ruby-physfs).
#
# This is a Ruby C-extension on top of PhysicsFS. The gem only ships an
# extconf.rb / Rakefile, both awkward to cross-compile, so we build its
# .cpp files via a small in-tree CMakeLists.txt (cmake/scripts/ruby-physfs/
# CMakeLists.txt). Output is libphysfs-ruby.a, which gets linked into
# libembedded-ruby so Init_physfs() becomes a reachable symbol.
#
# Depends on `physfs` (must be `include(Physfs.cmake)`'d first) for the
# libphysfs.a + physfs.h it links against. Depends implicitly on the Ruby
# headers under ${CMAKE_SOURCE_DIR}/external/include/ (provided by
# ruby-for-android's release archive).
#
# The embedder is responsible for calling Init_physfs() + rb_provide("physfs")
# from a RubyCustomExtInit callback. Missing wiring fails fast at link time
# with an "undefined reference to Init_physfs" error, so no runtime stub .rb
# is needed.

include(ExternalProject)
include("${CMAKE_CURRENT_LIST_DIR}/SubCMakeArgs.cmake")

if(NOT TARGET physfs)
    message(FATAL_ERROR
        "RubyPhysfs.cmake requires the `physfs` target — include Physfs.cmake first.")
endif()

set(RUBY_PHYSFS_VERSION "0.1.0")
set(RUBY_PHYSFS_GIT_REPO "https://github.com/Scorbutics/ruby-physfs.git")
# Pin to a specific tag/commit when the gem stabilizes; for now track master.
set(RUBY_PHYSFS_GIT_TAG "master")

set(RUBY_PHYSFS_PREFIX_DIR  "${CMAKE_BINARY_DIR}/ruby-physfs")
set(RUBY_PHYSFS_INSTALL_DIR "${RUBY_PHYSFS_PREFIX_DIR}/install")
set(RUBY_PHYSFS_LIB_OUTPUT  "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/libphysfs-ruby.a")

set(_RUBY_PHYSFS_SUB_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}/scripts/ruby-physfs")

# Resolve Ruby header search dirs that the gem's C++ sources include.
# Layout (from ruby-for-android's release archive, extracted by
# scripts/download-ruby-libs.sh):
#   external/include/                       — ruby.h, ruby/*.h, zlib.h, ...
#   external/include/${HOST}/${BUILD_TYPE_DIR}/ — platform-specific ruby/config.h
set(_RUBY_PHYSFS_RUBY_INCLUDES
    "${CMAKE_SOURCE_DIR}/external/include"
    "${CMAKE_SOURCE_DIR}/external/include/${HOST}/${BUILD_TYPE_DIR}"
)

# Pull physfs.h location from the IMPORTED `physfs` target.
get_target_property(_RUBY_PHYSFS_PHYSFS_INC physfs INTERFACE_INCLUDE_DIRECTORIES)

get_sub_cmake_args(_RUBY_PHYSFS_CMAKE_ARGS)

# CMAKE_ARGS values may contain ';' (the Ruby include dirs list), so escape
# them into a string the sub-CMake will re-split into a list. The sub-build's
# CMakeLists.txt accepts RUBY_INCLUDE_DIRS as a semicolon-list.
string(REPLACE ";" "$<SEMICOLON>" _RUBY_PHYSFS_RUBY_INCLUDES_ESCAPED
    "${_RUBY_PHYSFS_RUBY_INCLUDES}")

ExternalProject_Add(ruby-physfs_ep
    PREFIX            ${RUBY_PHYSFS_PREFIX_DIR}
    GIT_REPOSITORY    ${RUBY_PHYSFS_GIT_REPO}
    GIT_TAG           ${RUBY_PHYSFS_GIT_TAG}
    GIT_SHALLOW       TRUE
    # -S points at our in-tree CMakeLists.txt (NOT the cloned gem repo). The
    # gem source path is forwarded as -DGEM_SOURCE_DIR so the sub-build can
    # pick up ext/physfs/*.cpp.
    CONFIGURE_COMMAND ${CMAKE_COMMAND}
                      -S ${_RUBY_PHYSFS_SUB_CMAKE_DIR}
                      -B <BINARY_DIR>
                      ${_RUBY_PHYSFS_CMAKE_ARGS}
                      -DGEM_SOURCE_DIR=<SOURCE_DIR>
                      -DRUBY_INCLUDE_DIRS=${_RUBY_PHYSFS_RUBY_INCLUDES_ESCAPED}
                      -DPHYSFS_INCLUDE_DIR=${_RUBY_PHYSFS_PHYSFS_INC}
                      -DCMAKE_INSTALL_PREFIX=${RUBY_PHYSFS_INSTALL_DIR}
    BUILD_COMMAND     ${CMAKE_COMMAND} --build <BINARY_DIR>
    BUILD_BYPRODUCTS  ${RUBY_PHYSFS_INSTALL_DIR}/lib/libphysfs-ruby.a ${RUBY_PHYSFS_LIB_OUTPUT}
    # After install, copy the .a into ${CMAKE_LIBRARY_OUTPUT_DIRECTORY} so
    # gradle's copy-to-libs step picks it up alongside libembedded-ruby.a.
    INSTALL_COMMAND   ${CMAKE_COMMAND} --install <BINARY_DIR>
            COMMAND   ${CMAKE_COMMAND} -E make_directory ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
            COMMAND   ${CMAKE_COMMAND} -E copy_if_different
                        ${RUBY_PHYSFS_INSTALL_DIR}/lib/libphysfs-ruby.a
                        ${RUBY_PHYSFS_LIB_OUTPUT}
    DEPENDS           physfs_ep
)

# IMPORTED target so consumers (ruby-vm) can `target_link_libraries(... physfs-ruby)`
# exactly as before, regardless of where the .a came from.
add_library(physfs-ruby STATIC IMPORTED GLOBAL)
set_target_properties(physfs-ruby PROPERTIES
    IMPORTED_LOCATION ${RUBY_PHYSFS_LIB_OUTPUT}
)
add_dependencies(physfs-ruby ruby-physfs_ep)

message(STATUS "ruby-physfs: building from ${RUBY_PHYSFS_GIT_REPO}@${RUBY_PHYSFS_GIT_TAG} -> ${RUBY_PHYSFS_LIB_OUTPUT}")
