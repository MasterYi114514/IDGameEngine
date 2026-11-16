cmake_minimum_required(VERSION 4.1.0)

project(IDJson)

# ============================================================
# IDJson：JSON 解析与序列化（静态库）
# 普通库构建入口：本目标可以直接由 CMake 构建，不以先运行 ID 或解析清单为前提
# ============================================================

# 错误日志桥开关：ON 时 IDJson 内部链接 IDLog 并在解析失败时输出一条 error；
# OFF 时不包含任何 IDLog 头、不链接 IDLog，attach/detach 与输出路径都编译为空操作。
option(IDJSON_ENABLE_LOG "Enable the IDLog error-log bridge inside IDJson" ON)

file(GLOB_RECURSE IDJSON_SRC_FILES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

add_library(IDJson STATIC ${IDJSON_SRC_FILES})

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
else()
    target_compile_definitions(IDJson
        PRIVATE
            ID_JSON_ENABLE_LOG=0
    )
endif()

target_compile_features(IDJson
    PUBLIC
        cxx_std_20
)

set_target_properties(IDJson PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
    CXX_EXTENSIONS OFF
)

target_include_directories(IDJson
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(IDJson
    PUBLIC
        IDCommonHeaders
)

if(IDJSON_ENABLE_LOG)
    target_link_libraries(IDJson
        PRIVATE
            IDLog
    )
endif()
