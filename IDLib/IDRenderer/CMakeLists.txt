cmake_minimum_required(VERSION 4.1.0)

project(IDRenderer)
set(CMAKE_CXX_STANDARD 20)

set(USE_OPENGL ON)
set(LARGE_PROJECT ON)

# 宏定义
add_definitions(-DIDRENDERER_EXPORTS)
if(USE_OPENGL)
    add_definitions(-DIDRENDERER_USE_OPENGL)
endif()

# 查找源文件
file(
    GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/IDRenderer.hpp
)

set(LIBRARY_OUTPUT_PATH ${BIN_DIR})
add_library(IDRenderer SHARED
    ${SRC_FILES}
)

# 预编译头文件
target_precompile_headers(IDRenderer
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include/Core/IDRpch.hpp
)

# 头文件路径
target_include_directories(IDRenderer
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${IDLIB_DIR}/IDLog/include
        ${IDLIB_DIR}/IDMath/include
        ${IDLIB_DIR}/IDWindow/include
)

# 宏
if(LARGE_PROJECT)
    target_compile_definitions(IDRenderer PUBLIC IDRENDERER_LARGE_PROJECT)
endif()

# 链接库
target_link_directories(IDRenderer
    PRIVATE
        ${BIN_DIR}
)

target_link_libraries(IDRenderer
    PRIVATE
        IDLog
        IDMath
        IDWindow
)