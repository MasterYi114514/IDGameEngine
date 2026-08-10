cmake_minimum_required(VERSION 4.1.0)

project(IDAudio)
set(CMAKE_CXX_STANDARD 20)

add_definitions(-DIDAUDIO_EXPORTS)

file(GLOB_RECURSE IDAUDIO_SRC CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

set(LIBRARY_OUTPUT_PATH ${BIN_DIR})
add_library(IDAudio SHARED ${IDAUDIO_SRC} ${CMAKE_CURRENT_SOURCE_DIR}/include/IDAudio.hpp)

target_include_directories(IDAudio
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${IDLIB_DIR}/IDLog/include
        ${IDLIB_DIR}/IDAsset/include         # AudioData 定义（Loader/AudioLoader.hpp）
        ${VENDOR_DIR}/openal/include       # OpenAL 头文件目录
)

target_link_directories(IDAudio
    PRIVATE
        ${BIN_DIR}
)

# IDMath / IDAsset 是 PUBLIC 依赖（AudioEngine 公开 API 使用 Vec3 / AudioData）
# IDAsset 对 IDRenderer 为 PRIVATE 依赖，不会经此处传播为 IDAudio 消费方的 PUBLIC 依赖
target_link_libraries(IDAudio
    PUBLIC
        IDMath
        IDAsset
    PRIVATE
        IDLog
        OpenAL
)
