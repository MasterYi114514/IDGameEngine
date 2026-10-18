cmake_minimum_required(VERSION 3.20)

set(BENCH_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# 用 GLOB_RECURSE + CONFIGURE_DEPENDS：新增测试域源文件不必手改本文件，也不会漏编译
file(GLOB_RECURSE BENCH_SRC CONFIGURE_DEPENDS ${BENCH_DIR}/src/*.cpp)

add_executable(Benchmark ${BENCH_SRC})
target_include_directories(Benchmark PRIVATE ${BENCH_DIR}/include)

# Assemble 层（IDGameEngine 产品目标）落地后改成这一行：
# target_link_libraries(Benchmark PRIVATE IDGameEngine)
target_link_libraries(Benchmark PRIVATE ID IDPluginStub)

# report.txt 的路径以编译期宏注入（源码目录的绝对路径），保证与运行时的 CWD 无关
target_compile_definitions(Benchmark PRIVATE
    ID_BENCHMARK_REPORT_DIR="${BENCH_DIR}"
    ID_BENCH_CONFIG="$<CONFIG>"
    ID_BENCH_LTO=$<IF:$<BOOL:${ID_MONOLITHIC_BUILD}>,1,0>
    ID_BENCH_AVX2=$<IF:$<BOOL:${ID_ENABLE_AVX2}>,1,0>
    ID_BENCH_MONOLITHIC=$<IF:$<BOOL:${ID_MONOLITHIC_BUILD}>,1,0>
)

if(COMMAND id_apply_performance_flags)
    id_apply_performance_flags(Benchmark)
endif()
