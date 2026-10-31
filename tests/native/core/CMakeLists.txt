cmake_minimum_required(VERSION 3.19.2)

project("test-core")

# Core-level tests - tests Ruby VM functionality with fully static configuration
# This test ALWAYS builds with BUILD_WRAPPER_SHARED=OFF and BUILD_SHARED_LIBS=OFF
# to ensure a fully static binary that doesn't depend on any shared Ruby libraries.

add_executable(test_core
    test_core.c
)

# Regression: sentinel must reach the C side even when VMLogger log level is
# raised to ERROR (NDEBUG=1 / RUBY_VM_LOG_LEVEL=2). Otherwise every script
# burns the full logging_wait_for_sentinel timeout.
add_executable(test_sentinel_log_level
    test_sentinel_log_level.c
)

# Async dispatch queue: exercise basic dispatch, bounded drop under load,
# and recursive-printing-callback no-deadlock (the case the previous dup2
# swap-based design used to "protect" against by silently losing script
# writes during the swap window).
add_executable(test_dispatch_queue
    test_dispatch_queue.c
)

# VMLogger severity propagation: write tagged lines into the VMLOGGER pipe and
# verify the parser strips the <<<VMLOG:LEVEL>>> prefix and surfaces the level
# as log_level_t on the custom-output callback. Also covers default-level
# derivation for non-VMLOGGER streams.
add_executable(test_vmlogger_level
    test_vmlogger_level.c
)

# Remote DAP debugger: verifies the Ruby `debug` gem's TCP listener comes up
# when ruby_interpreter_enable_remote_debug is called, that an empty token is
# refused at the API surface, and that no listener exists without the call.
add_executable(test_remote_debug
    test_remote_debug.c
)

# Remote line-eval console: verifies the RemoteEval listener comes up, the
# cookie handshake works, basic eval round-trips, wrong cookies are dropped,
# and expose/attach gives access to a script-registered local Binding.
add_executable(test_remote_eval
    test_remote_eval.c
)

# Thread-per-interpreter cooperation: two RubyInterpreter wrappers (and
# therefore two interpreter_ids on the wire) running concurrently. One
# does a long-running until-loop, the other fires short sync scripts.
# Before the multiplexed fifo_interpreter refactor, this would deadlock
# at the first B-side sync script — A's eval was still holding the
# single shared socket_lock and the only Ruby eval frame.
add_executable(test_multi_interpreter
    test_multi_interpreter.c
)

# Same-interpreter FIFO waiter pairing: N pthreads fire concurrent sync
# scripts against ONE interpreter, each expecting its own unique exit
# code back. Verifies the result_waiter FIFO list in ruby-vm.c pairs
# each response with the correct waiter when many submitters share an
# interpreter_id (the hard case test_multi_interpreter doesn't cover).
add_executable(test_same_id_fifo
    test_same_id_fifo.c
)

# Per-interpreter LogListener routing: exercises the in-band tag parser,
# the registry, and the native-fallback path without spinning up a full
# Ruby VM. Regression test for the bug where ephemeral interpreters
# clobbered the persistent loader's listener slot via the old
# vm->log_listener swap-on-rebind dance.
add_executable(test_interpreter_listener_routing
    test_interpreter_listener_routing.c
)

# Order-tolerance: the Android consumer registers its listener BEFORE
# calling enableLogging (which is what eventually triggers logging_init).
# Regression test for the bug where the first registration tried to bring
# up the dispatch thread, hit !is_initialized, and rolled the entry back
# — silently dropping the consumer's onLogMessage path.
add_executable(test_listener_register_before_init
    test_listener_register_before_init.c
)

# Host events (VMHost.emit), C side: routing by interpreter, payload
# decoding, malformed lines, and no event dropped under log backpressure.
add_executable(test_host_events
    test_host_events.c
)

# VMHost round trip on a real VM: emit, and messages delivered by the
# host's own interpreter to a long-running script.
add_executable(test_host_channel
    test_host_channel.c
)

# CA certificates bundle (SSL_CERT_FILE on Android): every certificate file of a
# directory is concatenated, the bundle is rebuilt only when the directory changed.
add_executable(test_ca_certificates
    test_ca_certificates.c
)

# Ruby variant: the stdlib extracts under the ABI directory install.c uses
# (no longer hard-coded), the VM runs the configured Ruby minor, and psych /
# fiddle / openssl / zlib load with the libraries folded into libruby-ext.a.
add_executable(test_ruby_variant
    test_ruby_variant.c
)
target_compile_definitions(test_ruby_variant PRIVATE EXPECTED_RUBY_MINOR="${RUBY_VARIANT}")

# Link against assets library (static or shared depending on BUILD_SHARED_LIBS)
# The ruby-api-loader handles dlopen/dlsym for libembedded-ruby.so
# Link against embedded-ruby which combines logging, ruby-vm, and jni OBJECT libraries
target_link_libraries(test_core
    assets          # For assets_bootstrap() - handles extraction and library loading
    embedded-ruby   # For ruby-api-loader.h and all Ruby VM functionality
    dl              # For dlopen/dlsym in ruby-api-loader
)

target_link_libraries(test_sentinel_log_level
    assets
    embedded-ruby
    dl
)

target_link_libraries(test_dispatch_queue
    assets
    embedded-ruby
    dl
)

target_link_libraries(test_vmlogger_level
    assets
    embedded-ruby
    dl
)

target_link_libraries(test_remote_debug
    assets
    embedded-ruby
    dl
)

target_link_libraries(test_remote_eval
    assets
    embedded-ruby
    dl
)

target_link_libraries(test_multi_interpreter
    assets
    embedded-ruby
    dl
)

target_link_libraries(test_same_id_fifo
    assets
    embedded-ruby
    dl
)

target_link_libraries(test_interpreter_listener_routing
    assets
    embedded-ruby
    dl
)

target_link_libraries(test_listener_register_before_init
    assets
    embedded-ruby
    dl
)

target_link_libraries(test_host_events
    assets
    embedded-ruby
    dl
)

target_link_libraries(test_host_channel
    assets
    embedded-ruby
    dl
)

target_link_libraries(test_ca_certificates
    assets
    embedded-ruby
    dl
)

target_link_libraries(test_ruby_variant
    assets
    embedded-ruby
    dl
)

# Extra ZIPs (assets_register_extra_zip): extracted at the load path root, part
# of the installation version. The ZIP is made from extra_zip/ at configure.
# Entries relative to extra_zip/ (extra_zip_test/hello.rb)
set(EXTRA_ZIP_TEST_ZIP "${CMAKE_CURRENT_BINARY_DIR}/extra-zip-test.zip")
execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar cf "${EXTRA_ZIP_TEST_ZIP}" --format=zip extra_zip_test
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/extra_zip")
# An entry escaping the install directory ("../extra_zip_escape.rb")
set(EXTRA_ZIP_TEST_UNSAFE_ZIP "${CMAKE_CURRENT_SOURCE_DIR}/extra_zip/unsafe.zip")
add_executable(test_extra_zip
    test_extra_zip.c
)
target_compile_definitions(test_extra_zip PRIVATE
    EXTRA_ZIP_PATH="${EXTRA_ZIP_TEST_ZIP}"
    EXTRA_ZIP_UNSAFE_PATH="${EXTRA_ZIP_TEST_UNSAFE_ZIP}")
target_link_libraries(test_extra_zip
    assets
    embedded-ruby
    dl
)

# The released include directory alone (packaged_headers, what CI ships):
# compiled apart so that no include directory of the libraries leaks in
add_library(test_packaged_headers_obj OBJECT test_packaged_headers.c)
target_include_directories(test_packaged_headers_obj PRIVATE ${EMBEDDED_RUBY_VM_PACKAGED_INCLUDE_DIR})
add_dependencies(test_packaged_headers_obj packaged_headers)
add_executable(test_packaged_headers $<TARGET_OBJECTS:test_packaged_headers_obj>)
target_link_libraries(test_packaged_headers
    assets
    embedded-ruby
    dl
)

# For static builds, ensure libcrypt's dependency on libbsd is satisfied
# libcrypt requires arc4random_buf from libbsd, which requires libmd
# These are already linked via embedded-ruby's dependencies (from ruby-vm), but we need them AFTER -lcrypt
# Use link options to append them at the end of the link line
if(NOT BUILD_SHARED_LIBS AND UNIX AND NOT APPLE)
    # Determine the correct library directory based on HOST
    if(BUILD_SHARED_LIBS)
        set(TEST_BUILD_TYPE_DIR "shared")
    else()
        set(TEST_BUILD_TYPE_DIR "static")
    endif()
    set(TEST_EXTERNAL_LIBS_DIR ${CMAKE_SOURCE_DIR}/external/lib/${HOST}/${TEST_BUILD_TYPE_DIR})

    # Find the libraries using the correct HOST architecture
    find_library(BSD_LIB_TEST NAMES bsd PATHS ${TEST_EXTERNAL_LIBS_DIR} NO_DEFAULT_PATH)
    find_library(MD_LIB_TEST NAMES md PATHS ${TEST_EXTERNAL_LIBS_DIR} NO_DEFAULT_PATH)
    
    if(BSD_LIB_TEST AND MD_LIB_TEST)
        message(STATUS "Appending libbsd and libmd to end of link line for test_core")
        # Use target_link_options to append libraries at the very end
        # This ensures they come AFTER -lcrypt in the link order
        target_link_options(test_core PRIVATE
            "${BSD_LIB_TEST}"
            "${MD_LIB_TEST}"
        )
        target_link_options(test_sentinel_log_level PRIVATE
            "${BSD_LIB_TEST}"
            "${MD_LIB_TEST}"
        )
        target_link_options(test_dispatch_queue PRIVATE
            "${BSD_LIB_TEST}"
            "${MD_LIB_TEST}"
        )
        target_link_options(test_vmlogger_level PRIVATE
            "${BSD_LIB_TEST}"
            "${MD_LIB_TEST}"
        )
        target_link_options(test_remote_debug PRIVATE
            "${BSD_LIB_TEST}"
            "${MD_LIB_TEST}"
        )
        target_link_options(test_remote_eval PRIVATE
            "${BSD_LIB_TEST}"
            "${MD_LIB_TEST}"
        )
        target_link_options(test_multi_interpreter PRIVATE
            "${BSD_LIB_TEST}"
            "${MD_LIB_TEST}"
        )
        target_link_options(test_same_id_fifo PRIVATE
            "${BSD_LIB_TEST}"
            "${MD_LIB_TEST}"
        )
        target_link_options(test_interpreter_listener_routing PRIVATE
            "${BSD_LIB_TEST}"
            "${MD_LIB_TEST}"
        )
        target_link_options(test_listener_register_before_init PRIVATE
            "${BSD_LIB_TEST}"
            "${MD_LIB_TEST}"
        )
        target_link_options(test_host_events PRIVATE
            "${BSD_LIB_TEST}"
            "${MD_LIB_TEST}"
        )
        target_link_options(test_host_channel PRIVATE
            "${BSD_LIB_TEST}"
            "${MD_LIB_TEST}"
        )
        target_link_options(test_ruby_variant PRIVATE
            "${BSD_LIB_TEST}"
            "${MD_LIB_TEST}"
        )
        target_link_options(test_extra_zip PRIVATE
            "${BSD_LIB_TEST}"
            "${MD_LIB_TEST}"
        )
        target_link_options(test_packaged_headers PRIVATE
            "${BSD_LIB_TEST}"
            "${MD_LIB_TEST}"
        )
    endif()
endif()

# Include directories for headers
target_include_directories(test_core PRIVATE
    ${CMAKE_SOURCE_DIR}/include/private
    ${CMAKE_SOURCE_DIR}/include/public
)
target_include_directories(test_sentinel_log_level PRIVATE
    ${CMAKE_SOURCE_DIR}/include/private
    ${CMAKE_SOURCE_DIR}/include/public
)
target_include_directories(test_dispatch_queue PRIVATE
    ${CMAKE_SOURCE_DIR}/include/private
    ${CMAKE_SOURCE_DIR}/include/public
)
target_include_directories(test_vmlogger_level PRIVATE
    ${CMAKE_SOURCE_DIR}/include/private
    ${CMAKE_SOURCE_DIR}/include/public
)
target_include_directories(test_remote_debug PRIVATE
    ${CMAKE_SOURCE_DIR}/include/private
    ${CMAKE_SOURCE_DIR}/include/public
)
target_include_directories(test_remote_eval PRIVATE
    ${CMAKE_SOURCE_DIR}/include/private
    ${CMAKE_SOURCE_DIR}/include/public
)
target_include_directories(test_multi_interpreter PRIVATE
    ${CMAKE_SOURCE_DIR}/include/private
    ${CMAKE_SOURCE_DIR}/include/public
)
target_include_directories(test_same_id_fifo PRIVATE
    ${CMAKE_SOURCE_DIR}/include/private
    ${CMAKE_SOURCE_DIR}/include/public
)
target_include_directories(test_interpreter_listener_routing PRIVATE
    ${CMAKE_SOURCE_DIR}/include/private
    ${CMAKE_SOURCE_DIR}/include/public
)
target_include_directories(test_listener_register_before_init PRIVATE
    ${CMAKE_SOURCE_DIR}/include/private
    ${CMAKE_SOURCE_DIR}/include/public
)
target_include_directories(test_host_events PRIVATE
    ${CMAKE_SOURCE_DIR}/include/private
    ${CMAKE_SOURCE_DIR}/include/public
)
target_include_directories(test_host_channel PRIVATE
    ${CMAKE_SOURCE_DIR}/include/private
    ${CMAKE_SOURCE_DIR}/include/public
)
target_include_directories(test_ca_certificates PRIVATE
    ${CMAKE_SOURCE_DIR}/include/private
    ${CMAKE_SOURCE_DIR}/include/public
)
target_include_directories(test_ruby_variant PRIVATE
    ${CMAKE_SOURCE_DIR}/include/private
    ${CMAKE_SOURCE_DIR}/include/public
)
target_include_directories(test_extra_zip PRIVATE
    ${CMAKE_SOURCE_DIR}/include/private
    ${CMAKE_SOURCE_DIR}/include/public
)

# Set RPATH so the executable can find libembedded-ruby.so at runtime
if(UNIX)
    set_target_properties(test_core PROPERTIES
        BUILD_RPATH "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
        INSTALL_RPATH "$ORIGIN/../lib"
    )
    set_target_properties(test_sentinel_log_level PROPERTIES
        BUILD_RPATH "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
        INSTALL_RPATH "$ORIGIN/../lib"
    )
    set_target_properties(test_dispatch_queue PROPERTIES
        BUILD_RPATH "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
        INSTALL_RPATH "$ORIGIN/../lib"
    )
    set_target_properties(test_vmlogger_level PROPERTIES
        BUILD_RPATH "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
        INSTALL_RPATH "$ORIGIN/../lib"
    )
    set_target_properties(test_remote_debug PROPERTIES
        BUILD_RPATH "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
        INSTALL_RPATH "$ORIGIN/../lib"
    )
    set_target_properties(test_remote_eval PROPERTIES
        BUILD_RPATH "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
        INSTALL_RPATH "$ORIGIN/../lib"
    )
    set_target_properties(test_multi_interpreter PROPERTIES
        BUILD_RPATH "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
        INSTALL_RPATH "$ORIGIN/../lib"
    )
    set_target_properties(test_same_id_fifo PROPERTIES
        BUILD_RPATH "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
        INSTALL_RPATH "$ORIGIN/../lib"
    )
    set_target_properties(test_interpreter_listener_routing PROPERTIES
        BUILD_RPATH "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
        INSTALL_RPATH "$ORIGIN/../lib"
    )
    set_target_properties(test_listener_register_before_init PROPERTIES
        BUILD_RPATH "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
        INSTALL_RPATH "$ORIGIN/../lib"
    )
    set_target_properties(test_host_events PROPERTIES
        BUILD_RPATH "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
        INSTALL_RPATH "$ORIGIN/../lib"
    )
    set_target_properties(test_host_channel PROPERTIES
        BUILD_RPATH "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
        INSTALL_RPATH "$ORIGIN/../lib"
    )
    set_target_properties(test_ruby_variant PROPERTIES
        BUILD_RPATH "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
        INSTALL_RPATH "$ORIGIN/../lib"
    )
    set_target_properties(test_extra_zip PROPERTIES
        BUILD_RPATH "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
        INSTALL_RPATH "$ORIGIN/../lib"
    )
    set_target_properties(test_packaged_headers PROPERTIES
        BUILD_RPATH "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
        INSTALL_RPATH "$ORIGIN/../lib"
    )
endif()

# Note: No RPATH needed since we're using static linking for libassets.a
# The executable can be run directly without LD_LIBRARY_PATH for the assets library

# Register with CTest. TIMEOUT keeps a hung test from stalling the whole
# CI run — typical wall times are 6 s or less; 30 s gives ~5x headroom for
# slow runners while still failing fast on real hangs so CTest can print
# the captured stdout (LOGF) via --output-on-failure.
add_test(NAME test_core COMMAND test_core WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
add_test(NAME test_sentinel_log_level COMMAND test_sentinel_log_level WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
add_test(NAME test_dispatch_queue COMMAND test_dispatch_queue WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
add_test(NAME test_vmlogger_level COMMAND test_vmlogger_level WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
add_test(NAME test_remote_debug COMMAND test_remote_debug WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
add_test(NAME test_remote_eval COMMAND test_remote_eval WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
add_test(NAME test_multi_interpreter COMMAND test_multi_interpreter WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
add_test(NAME test_same_id_fifo COMMAND test_same_id_fifo WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
add_test(NAME test_interpreter_listener_routing COMMAND test_interpreter_listener_routing WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
add_test(NAME test_listener_register_before_init COMMAND test_listener_register_before_init WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
add_test(NAME test_host_events COMMAND test_host_events WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
add_test(NAME test_host_channel COMMAND test_host_channel WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
add_test(NAME test_ruby_variant COMMAND test_ruby_variant WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
add_test(NAME test_extra_zip COMMAND test_extra_zip WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
add_test(NAME test_packaged_headers COMMAND test_packaged_headers WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
add_test(NAME test_ca_certificates COMMAND test_ca_certificates WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

set_tests_properties(
    test_core
    test_sentinel_log_level
    test_dispatch_queue
    test_vmlogger_level
    test_remote_debug
    test_remote_eval
    test_multi_interpreter
    test_same_id_fifo
    test_interpreter_listener_routing
    test_listener_register_before_init
    test_host_events
    test_host_channel
    test_ruby_variant
    test_extra_zip
    test_packaged_headers
    test_ca_certificates
    PROPERTIES TIMEOUT 30
)
