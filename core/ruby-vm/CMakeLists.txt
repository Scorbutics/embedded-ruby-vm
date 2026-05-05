cmake_minimum_required(VERSION 3.19.2)

project("ruby-vm" C)

# Include the script embedding function
include(${CMAKE_CURRENT_SOURCE_DIR}/embed_script.cmake)

# Embed fifo_interpreter.rb into the library
embed_script(fifo_interpreter.rb EMBEDDED_FIFO_INTERPRETER)

# Embed safe_runner.rb into the library
embed_script(safe_runner.rb EMBEDDED_SAFE_RUNNER)

# Export the embedded script object files to CACHE so embedded-ruby can access them
set(RUBY_VM_EMBEDDED_SCRIPT_OBJ ${EMBEDDED_FIFO_INTERPRETER} ${EMBEDDED_SAFE_RUNNER} CACHE INTERNAL "Embedded script object files from ruby-vm")

# This will be populated after embedded-ruby is built
set(DEPENDENCY_LIST "    /* Dependencies will be populated in embedded-ruby library */")

add_library(ruby-vm OBJECT
    ruby-comm-channel.c
    env.c
    exec-main-vm.c
    ruby-interpreter.c
    ruby-script.c
    ruby-script-location.c
    ruby-vm.c
    ruby-vm-error.c
    embedded_scripts.c
    ${EMBEDDED_FIFO_INTERPRETER}
    ${EMBEDDED_SAFE_RUNNER}
    ${DEPS_C_OUT}
)

set_target_properties(ruby-vm PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    COMPILE_FLAGS "-Wall -Wextra -Werror"
    POSITION_INDEPENDENT_CODE ON
)

# Generate ruby-api-loader.h based on build type
# This ensures users get the correct implementation without needing to define RUBY_STATIC
if(BUILD_SHARED_LIBS)
    # Dynamic build - include embedded-ruby-vm/shared/ruby-api-loader.h
    set(RUBY_API_LOADER_INCLUDE "#include \"embedded-ruby-vm/shared/ruby-api-loader.h\"")
else()
    # Static build - include embedded-ruby-vm/static/ruby-api-loader.h
    set(RUBY_API_LOADER_INCLUDE "#include \"embedded-ruby-vm/static/ruby-api-loader.h\"")
endif()

configure_file(
    ${EMBEDDED_RUBY_VM_MAIN_INCLUDE_DIR}/public/embedded-ruby-vm/ruby-api-loader.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/embedded-ruby-vm/ruby-api-loader.h
    @ONLY
)

target_include_directories(ruby-vm
    PRIVATE
        ${IMPORT_DIR}
        ${EMBEDDED_RUBY_VM_MAIN_INCLUDE_DIR}/private
    PUBLIC
        ${EMBEDDED_RUBY_VM_MAIN_INCLUDE_DIR}/public
        ${CMAKE_CURRENT_BINARY_DIR}  # For generated ruby-api-loader.h
)

# Link against Ruby and its dependencies
# Different linking strategy for static vs shared builds
# Note: For OBJECT libraries, we only need to specify include dependencies here
# The actual linking will happen in the embedded-ruby target
if(BUILD_SHARED_LIBS)
    # Shared build: Link against shared libraries
    # Note: Removed 'curses' (it's just an alias/linker script for ncurses)
    # Shared build: libphysfs-ruby is C++ → pull in the right C++ runtime.
    # Android NDK uses libc++ (no libstdc++); Apple uses libc++; everything
    # else (glibc/musl Linux) defaults to libstdc++. Apple also needs the
    # IOKit + Foundation frameworks for libphysfs.a's Objective-C source.
    set(_RUBY_VM_PHYSFS_SHARED_LIBS "")
    if(EMBEDDED_RUBY_VM_WITH_PHYSFS)
        if(ANDROID)
            set(_RUBY_VM_PHYSFS_SHARED_LIBS physfs-ruby physfs c++_shared)
        elseif(APPLE)
            set(_RUBY_VM_PHYSFS_SHARED_LIBS
                physfs-ruby physfs c++
                "-framework IOKit"
                "-framework Foundation"
            )
        else()
            set(_RUBY_VM_PHYSFS_SHARED_LIBS physfs-ruby physfs stdc++)
        endif()
    endif()
    target_link_libraries(ruby-vm
        ruby
        ${_RUBY_VM_PHYSFS_SHARED_LIBS}
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
else()
    # Static build: Link against static libraries
    # CRITICAL: Use --whole-archive for ruby and ruby-ext to force inclusion of all symbols
    # This ensures Init_monitor, Init_readline, etc. are included even if not directly referenced
    # Order matters for static linking - list dependencies from highest to lowest level

    if(UNIX AND NOT APPLE)
        # Linux: Use --whole-archive for static extension libraries
        # Find libbsd, libmd, and libcrypt using the correct HOST architecture
        # Inherit BUILD_TYPE_DIR and HOST from parent scope
        if(BUILD_SHARED_LIBS)
            set(BUILD_TYPE_DIR "shared")
        else()
            set(BUILD_TYPE_DIR "static")
        endif()
        set(RUBY_VM_EXTERNAL_LIBS_DIR ${CMAKE_SOURCE_DIR}/external/lib/${HOST}/${BUILD_TYPE_DIR})

        find_library(FFI_LIBRARY NAMES ffi PATHS ${RUBY_VM_EXTERNAL_LIBS_DIR} NO_DEFAULT_PATH)
        if(NOT FFI_LIBRARY)
            find_library(FFI_LIBRARY NAMES ffi)
        endif()
        find_library(BSD_LIBRARY NAMES bsd PATHS ${RUBY_VM_EXTERNAL_LIBS_DIR} NO_DEFAULT_PATH)
        find_library(MD_LIBRARY NAMES md PATHS ${RUBY_VM_EXTERNAL_LIBS_DIR} NO_DEFAULT_PATH)
        find_library(CRYPT_LIBRARY NAMES crypt PATHS ${RUBY_VM_EXTERNAL_LIBS_DIR} NO_DEFAULT_PATH)
        
        if(FFI_LIBRARY)
            message(STATUS "Found libffi: ${FFI_LIBRARY}")
        else()
            message(WARNING "libffi not found - Ruby's fiddle extension may fail to link")
            set(FFI_LIBRARY "")
        endif()

        if(BSD_LIBRARY)
            message(STATUS "Found libbsd: ${BSD_LIBRARY}")
        else()
            message(WARNING "libbsd not found - libcrypt may fail to link")
            set(BSD_LIBRARY "")
        endif()

        if(MD_LIBRARY)
            message(STATUS "Found libmd: ${MD_LIBRARY}")
        else()
            message(WARNING "libmd not found - libbsd may fail to link")
            set(MD_LIBRARY "")
        endif()
        
        if(CRYPT_LIBRARY)
            message(STATUS "Found libcrypt: ${CRYPT_LIBRARY}")
        else()
            message(WARNING "libcrypt not found - will use system libcrypt")
        endif()
        
        # Build the PUBLIC libraries list
        set(RUBY_VM_PUBLIC_LIBS
            ruby           # libruby-static.a or libruby.a (imported target from core/)
            readline       # libreadline.a (contains history functions, so libhistory.a is not needed)
            ncurses        # libncurses.a (used by readline)
            ncurses++      # libncurses++.a
            panel          # libpanel.a
            menu           # libmenu.a
            form           # libform.a
            gdbm           # libgdbm.a
            gdbm_compat    # libgdbm_compat.a
            ssl            # libssl.a
            crypto         # libcrypto.a (used by ssl)
            gmp            # libgmp.a (used by Ruby for bignum operations)
        )
        
        # Add libcrypt with full path if found, otherwise use system
        if(CRYPT_LIBRARY)
            list(APPEND RUBY_VM_PUBLIC_LIBS "${CRYPT_LIBRARY}")
        else()
            list(APPEND RUBY_VM_PUBLIC_LIBS crypt)
        endif()
        
        # Add bsd and md if found (must come AFTER libcrypt for proper link order)
        # CRITICAL: libmd must come AFTER libbsd (libbsd depends on libmd)
        if(BSD_LIBRARY)
            list(APPEND RUBY_VM_PUBLIC_LIBS "${BSD_LIBRARY}")
        endif()

        if(MD_LIBRARY)
            list(APPEND RUBY_VM_PUBLIC_LIBS "${MD_LIBRARY}")
        endif()
        
        if(FFI_LIBRARY)
            list(APPEND RUBY_VM_PUBLIC_LIBS "${FFI_LIBRARY}")  # libffi (used by Ruby's fiddle extension)
        endif()

        list(APPEND RUBY_VM_PUBLIC_LIBS z)  # libz.a (used by many libraries)
        
        # Optionally fold ruby-physfs.a (and its libphysfs.a dep) into the
        # whole-archive group so Init_physfs survives unused-symbol stripping.
        # The host application calls it via ruby_set_custom_ext_init().
        #
        # libphysfs-ruby.a is a C++ archive (std::string, std::filesystem,
        # exception runtime). When the consumer links its final binary with a
        # plain C driver (gcc/clang), the C++ runtime is NOT pulled in
        # automatically — append it explicitly here. The right lib differs
        # by platform: Android NDK ships libc++ (no libstdc++ at all);
        # plain Linux (glibc/musl) typically uses libstdc++.
        set(_RUBY_VM_WHOLE_ARCHIVE_LIBS ruby-ext)
        if(EMBEDDED_RUBY_VM_WITH_PHYSFS)
            list(APPEND _RUBY_VM_WHOLE_ARCHIVE_LIBS physfs-ruby)
            list(APPEND RUBY_VM_PUBLIC_LIBS physfs)
            if(ANDROID)
                # NDK's clang++ links libc++_static.a by default for static
                # builds; for shared we'd take libc++_shared.so. Pick by
                # BUILD_SHARED_LIBS.
                if(BUILD_SHARED_LIBS)
                    list(APPEND RUBY_VM_PUBLIC_LIBS c++_shared)
                else()
                    list(APPEND RUBY_VM_PUBLIC_LIBS c++_static)
                endif()
            else()
                # glibc/musl Linux — libstdc++ is the GCC/Clang default.
                list(APPEND RUBY_VM_PUBLIC_LIBS stdc++)
            endif()
        endif()

        target_link_libraries(ruby-vm
            PRIVATE
            -Wl,--whole-archive
            ${_RUBY_VM_WHOLE_ARCHIVE_LIBS}  # libruby-ext.a + optionally libphysfs-ruby.a
            -Wl,--no-whole-archive
            PUBLIC
            ${RUBY_VM_PUBLIC_LIBS}
        )
    elseif(APPLE)
        # macOS: Use -force_load for static extension libraries
        # Resolve IMPORTED_LOCATION at configure time rather than using $<TARGET_FILE:...>
        # generator expressions, because these link flags are propagated via
        # $<TARGET_PROPERTY:ruby-vm,LINK_LIBRARIES> in the parent CMakeLists.txt,
        # and nested generator expressions inside $<TARGET_PROPERTY> are not evaluated.
        get_target_property(_ruby_lib ruby IMPORTED_LOCATION)
        get_target_property(_ruby_ext_lib ruby-ext IMPORTED_LOCATION)

        # Ruby's fiddle extension requires libffi. On macOS, Ruby uses the system libffi
        # rather than bundling it, so we need to find and link it explicitly.
        # Check the prebuilt external libs first, then fall back to system libffi.
        find_library(FFI_LIBRARY NAMES ffi PATHS ${EXTERNAL_LIBS_DIR} NO_DEFAULT_PATH)
        if(NOT FFI_LIBRARY)
            find_library(FFI_LIBRARY NAMES ffi)
        endif()
        if(FFI_LIBRARY)
            message(STATUS "Found libffi: ${FFI_LIBRARY}")
        else()
            # On Apple platforms, libffi is always available in the SDK.
            # find_library may fail during iOS cross-compilation because the .tbd
            # stub in the SDK sysroot isn't found, but the linker resolves -lffi
            # correctly via CMAKE_OSX_SYSROOT.
            message(STATUS "libffi not found by find_library - using bare linker flag (-lffi)")
            set(FFI_LIBRARY ffi)
        endif()

        # Optional ruby-physfs (force_load like ruby-ext for symbol retention).
        # Append libc++ so the C++ runtime symbols (std::string, std::filesystem,
        # __cxa_*, etc.) resolve when the embedder links its final binary with
        # a plain C driver. macOS / iOS use libc++; on Apple platforms it's the
        # default for clang regardless of -stdlib, so the bare name resolves.
        #
        # PhysFS's Apple platform source (physfs_platform_apple.m) calls into
        # IOKit (CD detection) and Foundation (NSBundle / NSSearchPath); link
        # those frameworks here so consumers of libphysfs.a don't have to
        # know about its Objective-C internals.
        set(_RUBY_VM_PHYSFS_FORCE_LOAD "")
        set(_RUBY_VM_PHYSFS_LINK_LIBS "")
        if(EMBEDDED_RUBY_VM_WITH_PHYSFS)
            get_target_property(_physfs_ruby_lib physfs-ruby IMPORTED_LOCATION)
            set(_RUBY_VM_PHYSFS_FORCE_LOAD "-Wl,-force_load,${_physfs_ruby_lib}")
            set(_RUBY_VM_PHYSFS_LINK_LIBS
                physfs
                c++
                "-framework IOKit"
                "-framework Foundation"
            )
        endif()

        target_link_libraries(ruby-vm
            -Wl,-force_load,${_ruby_ext_lib}
            ${_RUBY_VM_PHYSFS_FORCE_LOAD}
            ruby
            readline       # libreadline.a (contains history functions)
            ncurses
            ncurses++
            panel
            menu
            form
            gdbm
            gdbm_compat
            ssl
            crypto
            gmp            # libgmp.a (used by Ruby for bignum operations)
            z
            ${_RUBY_VM_PHYSFS_LINK_LIBS}   # libphysfs.a (when WITH_PHYSFS)
            ${FFI_LIBRARY}                 # libffi (used by Ruby's fiddle extension)
            "-framework CoreFoundation"    # Used by Ruby's file.o for path normalization
            # Note: macOS doesn't need libcrypt (functionality is in libc)
        )
    else()
        # Other platforms: fallback (may not work correctly).
        # libphysfs-ruby.a is C++; pull in the C++ runtime explicitly. Bare
        # `stdc++` works on most non-NDK Unixes; embedders on exotic targets
        # may need to override.
        set(_RUBY_VM_PHYSFS_FALLBACK_LIBS "")
        if(EMBEDDED_RUBY_VM_WITH_PHYSFS)
            set(_RUBY_VM_PHYSFS_FALLBACK_LIBS physfs-ruby physfs stdc++)
        endif()
        target_link_libraries(ruby-vm
            ruby
            ruby-ext
            ${_RUBY_VM_PHYSFS_FALLBACK_LIBS}
            readline       # libreadline.a (contains history functions)
            ncurses
            ncurses++
            panel
            menu
            form
            gdbm
            gdbm_compat
            ssl
            crypto
            gmp            # libgmp.a (used by Ruby for bignum operations)
            crypt          # libcrypt.a (platform-dependent)
            bsd            # libbsd.a (provides arc4random_buf for libcrypt)
            md             # libmd.a (required by libbsd)
            z
        )
    endif()
endif()
