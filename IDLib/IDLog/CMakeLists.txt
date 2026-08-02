cmake_minimum_required(VERSION 4.1.0)

project(IDLog)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 预处理宏
add_definitions(-DIDLOG_EXPORTS)        # 定义导出宏，编译库时使用

file(
    GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

set(LIBRARY_OUTPUT_PATH ${BIN_DIR})
add_library(IDLog SHARED ${SRC_FILES})

target_include_directories(IDLog
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${VENDOR_DIR}/spdlog/include
)