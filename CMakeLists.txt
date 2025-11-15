cmake_minimum_required(VERSION 3.19.2)

project("embedded-ruby-vm")

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}-${CMAKE_CXX_PLATFORM_ID}-${CMAKE_SYSTEM_NAME}" HOST)
message(NOTICE "Configuring for host: ${HOST}")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

set(IMPORT_DIR ${CMAKE_SOURCE_DIR}/external/include ${CMAKE_SOURCE_DIR}/external/include/${HOST})
set(JNILIBS_DIR ${CMAKE_SOURCE_DIR}/external/lib/${HOST})
set(LOGGING_DIR ${CMAKE_SOURCE_DIR}/logging)
set(RUBY_VM_DIR ${CMAKE_SOURCE_DIR}/ruby-vm)
set(ASSETS_DIR ${CMAKE_SOURCE_DIR}/assets)
set(JNI_DIR ${CMAKE_SOURCE_DIR}/jni)

find_library(log-lib log)
find_library(zlib-lib z)

#set(CMAKE_SHARED_LINKER_FLAGS "-landroid")

set(libs_sfml_dependencies sfml-activity sfml-system sfml-window sfml-graphics sfml-audio sfml-network openal ogg vorbis vorbisenc vorbisfile FLAC freetype)
set(libs_shared_dependencies ruby ${libs_sfml_dependencies})

FOREACH(lib_shared ${libs_shared_dependencies})
    add_library(${lib_shared} SHARED IMPORTED)
    set_target_properties(${lib_shared} PROPERTIES IMPORTED_LOCATION ${JNILIBS_DIR}/lib${lib_shared}.so)
ENDFOREACH()

add_subdirectory(logging)
add_subdirectory(assets)
add_subdirectory(ruby-vm)
#add_subdirectory(jni)

set(sfml-main ${JNILIBS_DIR}/libsfml-main.a)
add_library(embedded-ruby)
set_target_properties(embedded-ruby 
    PROPERTIES COMPILE_FLAGS "-Wall -Wextra -Werror"
)

target_link_libraries(embedded-ruby
        logging
        ruby-vm
        assets
)
#target_include_directories(psdk-app PRIVATE ${IMPORT_DIR} ${JNI_DIR})
