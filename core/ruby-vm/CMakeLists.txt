cmake_minimum_required(VERSION 3.19.2)

project("ruby-vm" C)

# Include the script embedding function
include(${CMAKE_CURRENT_SOURCE_DIR}/embed_script.cmake)

# Embed fifo_interpreter.rb into the library
embed_script(fifo_interpreter.rb EMBEDDED_FIFO_INTERPRETER)

# This will be populated after embedded-ruby is built
set(DEPENDENCY_LIST "    /* Dependencies will be populated in embedded-ruby library */")

add_library(ruby-vm STATIC
    ruby-comm-channel.c
    env.c
    exec-main-vm.c
    ruby-interpreter.c
    ruby-script.c
    ruby-script-location.c
    ruby-vm.c
    ruby-vm-error.c
    embedded_scripts.c
    glibc-compat.c
    ${EMBEDDED_FIFO_INTERPRETER}
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
    # Dynamic build - include shared/ruby-api-loader.h
    set(RUBY_API_LOADER_INCLUDE "#include \"shared/ruby-api-loader.h\"")
else()
    # Static build - include static/ruby-api-loader.h
    set(RUBY_API_LOADER_INCLUDE "#include \"static/ruby-api-loader.h\"")
endif()

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/ruby-api-loader.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/ruby-api-loader.h
    @ONLY
)

target_include_directories(ruby-vm
    PRIVATE
        ${IMPORT_DIR}
        ${LOGGING_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}  # For generated ruby-api-loader.h
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}  # For shared/ruby-api-loader.h and static/ruby-api-loader.h
        ${CMAKE_CURRENT_BINARY_DIR}  # For generated ruby-api-loader.h
)

# Link against Ruby and its dependencies
# Different linking strategy for static vs shared builds
if(BUILD_SHARED_LIBS)
    # Shared build: Link against shared libraries
    # Note: Removed 'curses' (it's just an alias/linker script for ncurses)
    target_link_libraries(ruby-vm
        logging
        ruby
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
    # CRITICAL: Use --whole-archive for ruby-static and ruby-ext to force inclusion of all symbols
    # This ensures Init_monitor, Init_readline, etc. are included even if not directly referenced
    # Order matters for static linking - list dependencies from highest to lowest level

    if(UNIX AND NOT APPLE)
        # Linux: Use --whole-archive for static extension libraries
        # Find libbsd, libmd, and libcrypt with full paths since HOST triplet detection may not match
        find_library(BSD_LIBRARY NAMES bsd PATHS ${CMAKE_SOURCE_DIR}/external/lib/*/static NO_DEFAULT_PATH)
        find_library(MD_LIBRARY NAMES md PATHS ${CMAKE_SOURCE_DIR}/external/lib/*/static NO_DEFAULT_PATH)
        find_library(CRYPT_LIBRARY NAMES crypt PATHS ${CMAKE_SOURCE_DIR}/external/lib/*/static NO_DEFAULT_PATH)
        
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
            ruby-static    # libruby-static.a contains core Ruby
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
        if(BSD_LIBRARY)
            list(APPEND RUBY_VM_PUBLIC_LIBS "${BSD_LIBRARY}")
        endif()
        if(MD_LIBRARY)
            list(APPEND RUBY_VM_PUBLIC_LIBS "${MD_LIBRARY}")
        endif()
        
        list(APPEND RUBY_VM_PUBLIC_LIBS z)  # libz.a (used by many libraries)
        
        target_link_libraries(ruby-vm
            PRIVATE
            logging
            -Wl,--whole-archive
            ruby-ext       # libruby-ext.a contains all statically-linked extensions (Init_monitor, etc.)
            -Wl,--no-whole-archive
            PUBLIC
            ${RUBY_VM_PUBLIC_LIBS}
        )
    elseif(APPLE)
        # macOS: Use -force_load for static extension libraries
        target_link_libraries(ruby-vm
            logging
            -Wl,-force_load,$<TARGET_FILE:ruby-static>
            -Wl,-force_load,$<TARGET_FILE:ruby-ext>
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
            # Note: macOS doesn't need libcrypt (functionality is in libc)
        )
    else()
        # Other platforms: fallback (may not work correctly)
        target_link_libraries(ruby-vm
            logging
            ruby-static
            ruby-ext
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
