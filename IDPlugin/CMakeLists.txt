cmake_minimum_required(VERSION 3.15)

project(IDPlugin)

add_library(IDCommonHeaders INTERFACE)

target_include_directories(IDCommonHeaders
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/Common
)

add_subdirectory(IDLog)