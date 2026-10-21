cmake_minimum_required(VERSION 3.20)

# =============================================================================
# P3 运行期 PluginRuntime 正确性校验（Benchmark 校验资产，不是生产目标）
#   - 只在显式开启 ID_BUILD_VALIDATION_RUNTIME 时进入构建，默认 OFF；
#   - 只使用生产头 Kernel/PluginRuntime.hpp 的公开 API，不修改被测实现；
#   - 输出走 ID::Bench 公共设施（BeginSession / Check / EndSession），报告目录指向本
#     构建目录的 out/，因此官方 Benchmark/report.txt 不会被本程序覆盖；
#   - 产物隔离：out/{bin,lib}；原始日志按构建目录名隔离到
#     Benchmark/validation/baseline/raw/runtime-<构建目录名>/，由 batch-owner.txt 绑定单一构建目录；
#   - 源文件在 validation/runtime/src 下，configure 期就校验它们没有进入普通 Benchmark 的
#     src/*.cpp glob，避免官方用例集被校验构建污染。
# =============================================================================
if(NOT ID_BUILD_VALIDATION_RUNTIME)
    return()
endif()

set(IDR_DIR     ${CMAKE_CURRENT_SOURCE_DIR})
set(IDR_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/out)
set(IDR_OUT_BIN ${IDR_OUT_DIR}/bin)
set(IDR_OUT_LIB ${IDR_OUT_DIR}/lib)

file(MAKE_DIRECTORY ${IDR_OUT_BIN} ${IDR_OUT_LIB})

set(IDR_SOURCES
    ${IDR_DIR}/src/runtime_main.cpp
    ${IDR_DIR}/src/RuntimeFixture.cpp
    ${IDR_DIR}/src/runtime_lifecycle_cases.cpp
    ${IDR_DIR}/src/runtime_service_cases.cpp
    ${IDR_DIR}/src/runtime_plan_cases.cpp
    ${IDR_DIR}/src/runtime_provision_cases.cpp)

# 普通 Benchmark 的源文件 glob 不得混入本目录：混入会让官方 report.txt 多出只有校验构建才有的用例
if(DEFINED BENCH_SRC)
    foreach(idr_source IN LISTS IDR_SOURCES)
        list(FIND BENCH_SRC "${idr_source}" idr_in_bench_src)
        if(NOT idr_in_bench_src EQUAL -1)
            message(FATAL_ERROR "运行期校验源文件被普通 Benchmark 的 src/*.cpp glob 捡到：${idr_source}")
        endif()
    endforeach()
else()
    message(FATAL_ERROR "运行期校验需要父目录 Benchmark/CMakeLists.txt 提供 BENCH_SRC")
endif()

add_executable(PluginRuntimeCases
    ${IDR_SOURCES}
    ${BENCH_DIR}/src/support/benchmark_support.cpp)

target_include_directories(PluginRuntimeCases PRIVATE ${IDR_DIR}/src ${BENCH_DIR}/include)
target_link_libraries(PluginRuntimeCases PRIVATE ID IDPluginStub)

# 报告目录指向本构建目录的 out/：官方 Benchmark/report.txt 只由 Benchmark 目标覆盖写
target_compile_definitions(PluginRuntimeCases PRIVATE
    ID_BENCHMARK_REPORT_DIR="${IDR_OUT_DIR}"
    ID_BENCH_CONFIG="$<CONFIG>"
    ID_BENCH_LTO=$<IF:$<BOOL:${ID_MONOLITHIC_BUILD}>,1,0>
    ID_BENCH_AVX2=$<IF:$<BOOL:${ID_ENABLE_AVX2}>,1,0>
    ID_BENCH_MONOLITHIC=$<IF:$<BOOL:${ID_MONOLITHIC_BUILD}>,1,0>
)

if(COMMAND id_apply_performance_flags)
    id_apply_performance_flags(PluginRuntimeCases)
endif()

set_target_properties(PluginRuntimeCases PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${IDR_OUT_BIN}"
    LIBRARY_OUTPUT_DIRECTORY "${IDR_OUT_LIB}"
    ARCHIVE_OUTPUT_DIRECTORY "${IDR_OUT_LIB}")

# SHARED 模式下把内核 DLL 复制到本子工程自己的 out/bin，控制程序才能独立运行。
# 只写本构建目录，不覆盖共享 bin 里的用户产物（K6：OFF/ON 共用 bin 的既有问题不在本轮修）。
if(NOT ID_MONOLITHIC_BUILD)
    add_custom_command(TARGET PluginRuntimeCases POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:ID>" "${IDR_OUT_BIN}/$<TARGET_FILE_NAME:ID>"
        COMMENT "copy kernel DLL into runtime validation out/bin")
endif()

# 原始日志目录按构建目录名派生：runtime 与 P0 prototype、P1 production 都不互相覆盖
get_filename_component(IDR_BUILD_DIR_NAME "${CMAKE_BINARY_DIR}" NAME)
string(REGEX REPLACE "^cmake-build-" "" IDR_RAW_LOG_MODE "${IDR_BUILD_DIR_NAME}")
if(IDR_RAW_LOG_MODE STREQUAL "")
    set(IDR_RAW_LOG_MODE "unnamed-build")
endif()
set(IDR_RAW_LOG_DIR ${IDR_DIR}/../baseline/raw/runtime-${IDR_RAW_LOG_MODE})
file(MAKE_DIRECTORY ${IDR_RAW_LOG_DIR})
message(STATUS "runtime 校验原始日志目录：${IDR_RAW_LOG_DIR}（构建目录 ${CMAKE_BINARY_DIR}）")
message(STATUS "runtime 校验报告目录：${IDR_OUT_DIR}（不覆盖 ${BENCH_DIR}/report.txt）")

add_custom_target(PluginRuntimeCasesRun
    COMMAND ${CMAKE_COMMAND}
        -DRUNTIME_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
        "-DBATCH_CWD=${CMAKE_CURRENT_BINARY_DIR}"
        "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
        "-DBUILD_TYPE=${CMAKE_BUILD_TYPE}"
        "-DMONOLITHIC=${ID_MONOLITHIC_BUILD}"
        "-DGENERATOR=${CMAKE_GENERATOR}"
        "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
        "-DCXX_COMPILER_ID=${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
        "-DRAW_LOG_DIR=${IDR_RAW_LOG_DIR}"
        "-DRAW_LOG_MODE=runtime-${IDR_RAW_LOG_MODE}"
        "-DRUNTIME_EXE=$<TARGET_FILE:PluginRuntimeCases>"
        "-DRUNTIME_REPORT=${IDR_OUT_DIR}/report.txt"
        "-DOFFICIAL_REPORT=${BENCH_DIR}/report.txt"
        -P ${IDR_DIR}/cmake/RunRuntimeCases.cmake
    DEPENDS PluginRuntimeCases
    VERBATIM
    COMMENT "runtime cases: run PluginRuntimeCases + verify exit/console/report + official report isolation")
