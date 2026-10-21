cmake_minimum_required(VERSION 3.20)

# =============================================================================
# ID.Log 终止语义校验（Benchmark 校验资产，不是生产目标）
#   - 只在显式开启 ID_BUILD_VALIDATION_LOG 时进入构建，默认 OFF；
#   - 只做官方进程内不能做的验证：Fatal 必终止、shutdown 之后写入旧句柄的误用后果；
#     父/子进程同一 exe，父进程按"退出码非 0 + stderr 点名文本"判定；
#   - 输出写本构建目录（out/validation-summary.txt 与 out/stderr-<case>.log），
#     不写 Benchmark/report.txt、不进官方 exe、不参与六档评级；
#   - 必须链接产品消费目标 IDGameEngine：Fatal 语义依赖真实的 IDLog 后端与桥，
#     因此本目录要求 ID_ASSEMBLY_PROFILE 选中的插件集合里包含 ID.Log。
# =============================================================================
if(NOT ID_BUILD_VALIDATION_LOG)
    return()
endif()

set(IDV_DIR     ${CMAKE_CURRENT_SOURCE_DIR})
set(IDV_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/out)
set(IDV_OUT_BIN ${IDV_OUT_DIR}/bin)
file(MAKE_DIRECTORY ${IDV_OUT_BIN})

if(NOT TARGET IDGameEngine OR NOT TARGET IDLog)
    message(FATAL_ERROR
        "ID_BUILD_VALIDATION_LOG=ON 需要 ID_ASSEMBLY_PROFILE 选中 ID.Log："
        "当前构建没有 IDGameEngine 或 IDLog 目标，Fatal 语义无法被真实后端验证。"
        "请用 -DID_ASSEMBLY_PROFILE=log 重新配置（例如 Benchmark/build/log-rev2）。")
endif()

add_executable(IDLogFatalValidation ${IDV_DIR}/log_fatal_validation.cpp)

# 只经产品消费目标取头文件与身份宏：不在本文件重复注入宏或 include 路径
target_link_libraries(IDLogFatalValidation PRIVATE IDGameEngine)
target_compile_definitions(IDLogFatalValidation PRIVATE ID_LOG_VALIDATION_DIR="${IDV_OUT_DIR}")

if(COMMAND id_apply_performance_flags)
    id_apply_performance_flags(IDLogFatalValidation)
endif()

set_target_properties(IDLogFatalValidation PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${IDV_OUT_BIN}"
    LIBRARY_OUTPUT_DIRECTORY "${IDV_OUT_DIR}/lib"
    ARCHIVE_OUTPUT_DIRECTORY "${IDV_OUT_DIR}/lib")

# SHARED 模式下把内核与 IDLog 的 DLL 复制到**可执行文件所在目录**：父子两个进程都必须能独立加载。
# 目标目录必须用 $<TARGET_FILE_DIR:...> 而不是 IDV_OUT_BIN：多配置生成器（Visual Studio）会给
# RUNTIME_OUTPUT_DIRECTORY 追加 /<CONFIG>，exe 落在 out/bin/Release，直接写 IDV_OUT_BIN 会把 DLL
# 复制到 out/bin，运行时找不到（实测过的同类问题见 validation/prototype 的 idf_copy_kernel_runtime）。
if(NOT ID_MONOLITHIC_BUILD)
    add_custom_command(TARGET IDLogFatalValidation POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:ID>" "$<TARGET_FILE_DIR:IDLogFatalValidation>/$<TARGET_FILE_NAME:ID>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:IDLog>" "$<TARGET_FILE_DIR:IDLogFatalValidation>/$<TARGET_FILE_NAME:IDLog>"
        COMMENT "copy ID/IDLog DLLs next to IDLogFatalValidation")
endif()

message(STATUS "validation/log 输出目录：${IDV_OUT_DIR}（不覆盖 ${BENCH_DIR}/report.txt）")
