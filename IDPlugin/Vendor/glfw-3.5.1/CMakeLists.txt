cmake_minimum_required(VERSION 3.16...3.28)
project(GLFW VERSION 3.5.1 LANGUAGES C)

# 这份 vendor 副本只作为 IDWindow 的子目录接入：独立 configure 直接失败
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    message(FATAL_ERROR "vendor GLFW 只能通过 add_subdirectory 接入（配置由 IDWindow 负责）")
endif()

if(WIN32)
    # Win32 后端写死，无开关
elseif(APPLE)
    # Cocoa 后端写死，无开关
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    option(GLFW_BUILD_WAYLAND "构建 Wayland 后端（需要 wayland-scanner 与 wayland-*/xkbcommon 开发包）" OFF)
    set(GLFW_BUILD_X11 ON)
else()
    message(FATAL_ERROR "vendor GLFW 仅支持 Windows / macOS / Linux，当前 ${CMAKE_SYSTEM_NAME}")
endif()

# 头文件版本必须与 project() 版本一致：换库或裁剪失误在配置期暴露
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/GLFW/glfw3.h" _glfw_version_lines
     REGEX "^#define GLFW_VERSION_(MAJOR|MINOR|REVISION) +[0-9]+$")
string(CONCAT _glfw_version_text ${_glfw_version_lines})
foreach(_part MAJOR MINOR REVISION)
    string(REGEX MATCH "#define GLFW_VERSION_${_part} +([0-9]+)" _match "${_glfw_version_text}")
    set(_header_${_part} "${CMAKE_MATCH_1}")
endforeach()
if(NOT "${_header_MAJOR}.${_header_MINOR}.${_header_REVISION}" STREQUAL "${PROJECT_VERSION}")
    message(FATAL_ERROR "vendor GLFW 版本不一致：glfw3.h 为 ${_header_MAJOR}.${_header_MINOR}.${_header_REVISION}，project() 为 ${PROJECT_VERSION}")
endif()

find_package(Threads REQUIRED)
if(TARGET glfw)
    message(FATAL_ERROR "glfw 目标已存在：GLFW 只能由 IDWindow 接入一次")
endif()
add_subdirectory(src)
