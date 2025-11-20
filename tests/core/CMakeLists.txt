cmake_minimum_required(VERSION 3.19.2)

project("test-core")

# Core-level tests - tests Ruby VM functionality only
add_executable(test_core test_core.c)

target_link_libraries(test_core
    core
)

# Register with CTest
add_test(NAME test_core COMMAND test_core)
