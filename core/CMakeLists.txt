cmake_minimum_required(VERSION 3.19.2)

project("core")

set(IMPORT_DIR 
    ${CMAKE_CURRENT_SOURCE_DIR}/external/include 
    ${CMAKE_CURRENT_SOURCE_DIR}/external/include/${HOST}
)
set(LOGGING_DIR ${CMAKE_CURRENT_SOURCE_DIR}/logging)
set(RUBY_VM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ruby-vm)
set(ASSETS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets)
set(EXTERNAL_LIBS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/lib/${HOST})

set(libs_shared_dependencies ruby)

FOREACH(lib_shared ${libs_shared_dependencies})
    add_library(${lib_shared} SHARED IMPORTED)
    set_target_properties(${lib_shared} PROPERTIES IMPORTED_LOCATION ${EXTERNAL_LIBS_DIR}/lib${lib_shared}.so)
ENDFOREACH()

add_subdirectory(logging)
add_subdirectory(assets)
add_subdirectory(ruby-vm)

add_library(core INTERFACE)

target_link_libraries(core INTERFACE
        logging
        ruby-vm
        assets
)
target_include_directories(core INTERFACE 
    ${RUBY_VM_DIR}
    ${LOGGING_DIR}
    ${IMPORT_DIR}
)
