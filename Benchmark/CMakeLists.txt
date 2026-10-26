cmake_minimum_required(VERSION 3.15)

project(Benchmark)

# 测试源码用 CONFIGURE_DEPENDS 通配收集：新增域文件不需要手改本文件
file(GLOB_RECURSE BENCHMARK_SRC_FILES CONFIGURE_DEPENDS
    "src/*.cpp"
)

add_executable(Benchmark ${BENCHMARK_SRC_FILES})

target_include_directories(Benchmark
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 被测插件按普通库方式消费：公开头 + CMake 目标（不经过 ID 的生成接线，也不依赖运行期服务槽）
target_link_libraries(Benchmark
    PRIVATE
        IDCommonHeaders
        IDLog
)

target_compile_features(Benchmark
    PRIVATE
        cxx_std_20
)

# ---- 构建身份与路径注入：报告路径是绝对路径，与工作目录无关；缺失时实现侧显示 unknown ----
set(BENCHMARK_REPORT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/report.txt"
    CACHE FILEPATH "官方报告路径（绝对路径）")
set(BENCHMARK_LINK_MODE "SHARED"
    CACHE STRING "被测插件的链接形态（仅用于报告头；本插件为 SHARED）")

if(CMAKE_INTERPROCEDURAL_OPTIMIZATION)
    set(BENCHMARK_LTO_VALUE "ON")
else()
    set(BENCHMARK_LTO_VALUE "OFF")
endif()

target_compile_definitions(Benchmark
    PRIVATE
        BENCHMARK_REPORT_PATH="${BENCHMARK_REPORT_FILE}"
        BENCHMARK_TEMP_DIR="${CMAKE_CURRENT_BINARY_DIR}/tmp"
        BENCHMARK_BUILD_CONFIG="$<CONFIG>"
        BENCHMARK_LINK_MODE="${BENCHMARK_LINK_MODE}"
        BENCHMARK_LTO="${BENCHMARK_LTO_VALUE}"
        BENCHMARK_ARCH="${CMAKE_SYSTEM_PROCESSOR}"
)

# IDLog 是 SHARED：把运行期 DLL 复制到测试可执行文件旁边，避免依赖 PATH 或工作目录
add_custom_command(TARGET Benchmark POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:IDLog>"
            "$<TARGET_FILE_DIR:Benchmark>"
    COMMENT "copy IDLog shared library next to the Benchmark executable"
)
