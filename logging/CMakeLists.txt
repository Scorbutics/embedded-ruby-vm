cmake_minimum_required(VERSION 3.19.2)

project("logging" C)

add_library(logging OBJECT logging.c)
set_target_properties(logging PROPERTIES COMPILE_FLAGS "-Wall -Wextra -Werror")
