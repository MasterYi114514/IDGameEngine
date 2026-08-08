cmake_minimum_required(VERSION 4.1.0)

project(IDAsset)
set(CMAKE_CXX_STANDARD 20)

add_definitions(-DIDASSET_EXPORTS)

file(GLOB_RECURSE IDASSET_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

set(LIBRARY_OUTPUT_PATH ${BIN_DIR})
add_library(IDAsset SHARED ${IDASSET_SRC} ${CMAKE_CURRENT_SOURCE_DIR}/include/IDAsset.hpp)

target_include_directories(IDAsset
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${IDLIB_DIR}/IDLog/include
        ${IDLIB_DIR}/IDJson/include
        ${IDLIB_DIR}/IDRenderer/include          # TextureManager/ShaderManager 等
        ${VENDOR_DIR}/stb                        # stb_image.h
)

target_link_directories(IDAsset
    PRIVATE
        ${BIN_DIR}
)

target_link_libraries(IDAsset
    PRIVATE
        IDLog
        IDJson
    PUBLIC
        IDRenderer
)

target_compile_definitions(IDAsset PRIVATE -DASSIMP_DLL)   # assimp 为动态库，消费方需 dllimport
target_compile_definitions(IDAsset PRIVATE -DIDASSET_USE_ASSIMP)
# assimp 仅用于 IDAsset 内部实现（MeshLoader），不对外暴露，必须 PRIVATE 链接
# 避免 PUBLIC 传播导致 ID 主库 / Sandbox 也被拖入 assimp 依赖
target_link_libraries(IDAsset PRIVATE assimp::assimp)