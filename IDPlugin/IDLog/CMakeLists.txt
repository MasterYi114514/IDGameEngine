cmake_minimum_required(VERSION 4.1.0)

project(IDLog)

# src/*.hpp 是本插件的私有头：一起收进源文件列表只用于工程视图，公开头不参与编译
file(GLOB_RECURSE IDLOG_SRC_FILES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

add_library(IDLog SHARED ${IDLOG_SRC_FILES})

# DLL 自身必须以导出身份编译；缺少此项会让 DLL 内的符号被声明成 dllimport
target_compile_definitions(IDLog
    PRIVATE
        ID_LOG_EXPORTS
)

target_compile_features(IDLog
    PUBLIC
        cxx_std_20
)

set_target_properties(IDLog PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
    CXX_EXTENSIONS OFF
)

target_include_directories(IDLog
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# 三方日志库头随本目标传递；SYSTEM 避免三方头影响本工程的警告策略
target_include_directories(IDLog
    SYSTEM PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/spdlog/include
)

target_link_libraries(IDLog
    PUBLIC
        IDCommonHeaders
)
