cmake_minimum_required(VERSION 4.1.0)

project(glad)

set(LIBRARY_OUTPUT_PATH ${BIN_DIR})
add_library(glad SHARED ${CMAKE_CURRENT_SOURCE_DIR}/src/glad.c)

target_include_directories(glad 
    PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)