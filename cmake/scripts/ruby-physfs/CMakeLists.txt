# Builds libphysfs-ruby.a — the C++ side of the ruby-physfs gem.
#
# The gem ships only an extconf.rb / Rakefile, which is awkward to
# cross-compile. Its native part is just a handful of .cpp files we build
# directly with the cross toolchain. The output static lib gets linked into
# embedded-ruby-vm so that Init_physfs() becomes a reachable symbol — see
# ../../RubyPhysfs.cmake for how the embedder calls it.
#
# Required cache vars (passed via -D from RubyPhysfs.cmake):
#   GEM_SOURCE_DIR        Root of the cloned ruby-physfs repo (contains ext/physfs).
#   RUBY_INCLUDE_DIRS     Semicolon-separated list of dirs with Ruby headers
#                         (ruby.h, ruby/*.h, ruby/config.h, ...).
#   PHYSFS_INCLUDE_DIR    Directory containing physfs.h (from the locally built
#                         libphysfs sub-project).

cmake_minimum_required(VERSION 3.14)
project(ruby_physfs LANGUAGES CXX)

foreach(_v GEM_SOURCE_DIR RUBY_INCLUDE_DIRS PHYSFS_INCLUDE_DIR)
    if(NOT ${_v})
        message(FATAL_ERROR "ruby-physfs: -D${_v}=... is required")
    endif()
endforeach()

file(GLOB _SRCS CONFIGURE_DEPENDS "${GEM_SOURCE_DIR}/ext/physfs/*.cpp")
if(NOT _SRCS)
    message(FATAL_ERROR "ruby-physfs: no .cpp sources under ${GEM_SOURCE_DIR}/ext/physfs/")
endif()

add_library(physfs-ruby STATIC ${_SRCS})
target_compile_features(physfs-ruby PRIVATE cxx_std_17)
set_target_properties(physfs-ruby PROPERTIES POSITION_INDEPENDENT_CODE ON)

# RUBY_INCLUDE_DIRS arrives as a single string with ';' separators; CMake
# accepts that directly as a list for target_include_directories.
target_include_directories(physfs-ruby PRIVATE
    ${RUBY_INCLUDE_DIRS}
    ${PHYSFS_INCLUDE_DIR}
)

install(TARGETS physfs-ruby ARCHIVE DESTINATION lib)
