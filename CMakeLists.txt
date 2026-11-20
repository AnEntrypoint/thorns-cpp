cmake_minimum_required(VERSION 3.25)
project(thorns_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(THORNS_STATIC "Link the runtime statically" ON)
set(THORNS_MAX_BYTES 2097152 CACHE STRING "Size gate for the release binary, in bytes")

add_executable(thorns src/main.cpp)
target_compile_options(thorns PRIVATE -Wall -Wextra -Wpedantic)

target_compile_options(thorns PRIVATE $<$<CONFIG:Release>:-Os -ffunction-sections -fdata-sections -flto>)
target_link_options(thorns PRIVATE $<$<CONFIG:Release>:-flto -Wl,--gc-sections -s>)
if(THORNS_STATIC)
    target_link_options(thorns PRIVATE -static)
endif()

add_custom_target(size_gate ALL
    COMMAND ${CMAKE_COMMAND} -DFILE=$<TARGET_FILE:thorns> -DMAX=${THORNS_MAX_BYTES}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/size_gate.cmake
    DEPENDS thorns
    VERBATIM)
