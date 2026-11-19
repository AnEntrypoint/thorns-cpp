cmake_minimum_required(VERSION 3.25)
project(thorns_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(DEOXIDENE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../deoxidene" CACHE PATH "deoxidene checkout")

add_executable(thorns src/main.cpp)
target_include_directories(thorns PRIVATE "${DEOXIDENE_DIR}/include")
target_compile_options(thorns PRIVATE -Wall -Wextra -Wpedantic)

target_compile_options(thorns PRIVATE $<$<CONFIG:Release>:-Os -ffunction-sections -fdata-sections -flto>)
target_link_options(thorns PRIVATE $<$<CONFIG:Release>:-flto -Wl,--gc-sections -s>)
