cmake_minimum_required(VERSION 4.1.0)

project(IDWindow)
set(CMAKE_CXX_STANDARD 20)

# ---- 后端选择（默认 GLFW）----
option(IDWINDOW_USE_GLFW "Use GLFW + glad as window backend" ON)

if(IDWINDOW_USE_GLFW)
    add_definitions(-DIDWINDOW_USE_GLFW)
endif()

# ---- 导出宏 ----
add_definitions(-DIDWINDOW_EXPORTS)

# ---- 源文件 ----
file(
    GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

set(LIBRARY_OUTPUT_PATH ${BIN_DIR})
add_library(IDWindow SHARED
    ${SRC_FILES}
    ${CMAKE_CURRENT_SOURCE_DIR}/include/IDWindow.hpp
)

# ---- include 路径 ----
target_include_directories(IDWindow
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${IDLIB_DIR}/IDLog/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(IDWINDOW_USE_GLFW)
    target_include_directories(IDWindow
        PUBLIC
            ${VENDOR_DIR}/glad/include
            ${VENDOR_DIR}/glfw/include
    )
endif()

target_link_directories(IDWindow
    PRIVATE
        ${BIN_DIR}
)

target_link_libraries(IDWindow
    PRIVATE
        IDLog
)

# 后端相关的链接库
if(IDWINDOW_USE_GLFW)
    target_link_libraries(IDWindow
        PUBLIC
            glfw
            glad
    )

    if(WIN32)
        # ImmAssociateContext（禁用 IME 输入上下文）
        target_link_libraries(IDWindow PRIVATE imm32)
    endif()
endif()
