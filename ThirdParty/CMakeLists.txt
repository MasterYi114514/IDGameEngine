cmake_minimum_required(VERSION 4.1.0)

project(ThirdParty)

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

