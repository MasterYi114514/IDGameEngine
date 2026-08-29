cmake_minimum_required(VERSION 4.1.0)

project(ThirdParty)

# glad — 静态链接（纯 GL 函数指针加载器，无全局状态）
set(BUILD_SHARED_LIBS OFF)
add_subdirectory(glad)

# glfw — 必须编译为共享库（DLL）
# 原因：若为静态库，会被分别链接进 libIDWindowd.dll（创建窗口）、libIDd.dll（imgui_impl_glfw）、
# Sandbox.exe 三份副本，各持独立 GLFW 全局状态，导致 imgui 的 glfwGetWin32Window 找不到
# IDWindow 创建的窗口（hwnd=null，断言崩溃）。共享库保证全局状态唯一。
# 注意：不能用 BUILD_SHARED_LIBS（顶层 CMake 的普通变量优先于 cache），需用 GLFW_LIBRARY_TYPE
set(GLFW_LIBRARY_TYPE SHARED CACHE STRING "Library type override for GLFW" FORCE)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${BIN_DIR})
add_subdirectory(glfw)
set(GLFW_LIBRARY_TYPE "" CACHE STRING "Library type override for GLFW" FORCE)

# stb 纯头文件库
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/stb)

# assimp - 动态库（避免 174MB 静态链接进 libIDAssetd.dll）
set(BUILD_SHARED_LIBS ON)
set(ASSIMP_BUILD_TESTS OFF)
set(ASSIMP_INSTALL OFF)

# 让 assimp 的 DLL / 导入库输出到 bin/，与其它 ID 库保持一致
# assimp 内部用 ASSIMP_*_OUTPUT_DIRECTORY CACHE 变量，需同时覆盖，否则会输出到 build 目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${BIN_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${BIN_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${BIN_DIR})
set(ASSIMP_RUNTIME_OUTPUT_DIRECTORY ${BIN_DIR})
set(ASSIMP_LIBRARY_OUTPUT_DIRECTORY ${BIN_DIR})
set(ASSIMP_ARCHIVE_OUTPUT_DIRECTORY ${BIN_DIR})

add_subdirectory(assimp)

# Bullet3 — 物理引擎（静态库）
set(BUILD_SHARED_LIBS OFF)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(BUILD_BULLET2_DEMOS OFF CACHE BOOL "" FORCE)
set(BUILD_BULLET3 OFF CACHE BOOL "" FORCE)
set(BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(USE_GRAPHICAL_BENCHMARK OFF CACHE BOOL "" FORCE)
set(BULLET2_MULTITHREADING OFF CACHE BOOL "" FORCE)

add_subdirectory(bullet3)

# OpenAL-Soft — 静态库（音频后端）
set(BUILD_SHARED_LIBS OFF)
set(LIBTYPE STATIC CACHE STRING "" FORCE)
set(ALSOFT_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ALSOFT_TESTS OFF CACHE BOOL "" FORCE)
set(ALSOFT_UTILS OFF CACHE BOOL "" FORCE)
set(ALSOFT_NO_CONFIG_UTIL OFF CACHE BOOL "" FORCE)
add_subdirectory(openal)

# Dear ImGui — 调试 UI 库（静态库）
add_library(imgui STATIC
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_widgets.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_demo.cpp
    imgui/backends/imgui_impl_glfw.cpp
    imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui
    PUBLIC
        imgui
        imgui/backends
)
target_compile_definitions(imgui PRIVATE IMGUI_IMPL_OPENGL_LOADER_GLAD)
target_link_libraries(imgui PUBLIC glfw glad)

# imgui-node-editor — 节点编辑器（静态库，仿 imgui 手写目标，不用其自带 CMake）
add_library(imgui_node_editor STATIC
    imgui-node-editor/imgui_node_editor.cpp
    imgui-node-editor/imgui_node_editor_api.cpp
    imgui-node-editor/imgui_canvas.cpp
    imgui-node-editor/crude_json.cpp
)
target_include_directories(imgui_node_editor
    PUBLIC
        imgui-node-editor
)
target_compile_features(imgui_node_editor PRIVATE cxx_std_17)
target_link_libraries(imgui_node_editor PUBLIC imgui)

