cmake_minimum_required(VERSION 4.1.0)

project(ThirdParty)

# glad + glfw — 始终静态链接
set(BUILD_SHARED_LIBS OFF)
add_subdirectory(glad)
add_subdirectory(glfw)

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

