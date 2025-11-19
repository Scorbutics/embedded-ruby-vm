cmake_minimum_required(VERSION 3.19.2)

project("tests")

add_executable(tests test.c)

target_link_libraries(tests
        core
)