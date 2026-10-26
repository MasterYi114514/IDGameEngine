cmake_minimum_required(VERSION 3.15)

project(IDLog)

# 只收实现源码：公开头不是编译单元，vendor 头由 include 路径提供
file(GLOB IDLOG_SRC_FILES CONFIGURE_DEPENDS
    "src/*.cpp"
)

add_library(IDLog SHARED ${IDLOG_SRC_FILES})

# DLL 自身必须以导出身份编译；缺少此项会让 DLL 内的符号被声明成 dllimport
target_compile_definitions(IDLog
    PRIVATE
        ID_LOG_EXPORTS
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

target_compile_features(IDLog
    PUBLIC
        cxx_std_20
)
