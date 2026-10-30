cmake_minimum_required(VERSION 3.15)

project(IDJson)

# ============================================================
# IDJson：JSON 解析与序列化（静态库）
# 普通库构建入口：本目标可以直接由 CMake 构建，不以先运行 ID 或解析清单为前提
# ============================================================

# 错误日志桥开关：ON 时 IDJson 内部链接 IDLog 并在解析失败时输出一条 error；
# OFF 时不包含任何 IDLog 头、不链接 IDLog，attach/detach 与输出路径都编译为空操作。
option(IDJSON_ENABLE_LOG "Enable the IDLog error-log bridge inside IDJson" ON)

# 显式列源文件（不用 GLOB）：新增文件必须同时改这里，避免"改了源码没重新配置就静默漏编"
add_library(IDJson STATIC
    src/IDJsonDocument.cpp
    src/IDJsonError.cpp
    src/IDJsonLogBridge.cpp
    src/IDJsonParser.cpp
    src/IDJsonWriter.cpp
)

target_include_directories(IDJson
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# 静态库形态：公开头按"无需导入/导出声明"编译（见 include/IDJsonCore.hpp）
target_compile_definitions(IDJson
    PUBLIC
        ID_JSON_STATIC
)

if(IDJSON_ENABLE_LOG)
    # 开关只作用于本库自己的翻译单元：公开头不含任何 IDLog 类型，因此与消费者之间不存在 ODR 差异
    target_compile_definitions(IDJson
        PRIVATE
            ID_JSON_ENABLE_LOG=1
    )

    target_link_libraries(IDJson
        PRIVATE
            IDLog
    )
else()
    target_compile_definitions(IDJson
        PRIVATE
            ID_JSON_ENABLE_LOG=0
    )
endif()

target_link_libraries(IDJson
    PUBLIC
        IDCommonHeaders
)

target_compile_features(IDJson
    PUBLIC
        cxx_std_20
)

set_target_properties(IDJson
    PROPERTIES
        CXX_EXTENSIONS OFF
)
