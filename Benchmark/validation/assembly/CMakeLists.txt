cmake_minimum_required(VERSION 3.20)

# =============================================================================
# P2 组装层校验（Benchmark 校验资产，不是生产目标）
#   - 只在显式开启 ID_BUILD_VALIDATION_ASSEMBLY 时进入构建，默认 OFF；
#   - 本目录不新增任何编译单元，只注册一个批量驱动器目标 IDAssemblyValidationRun：
#     由它在自己的 work 目录下另行配置 STATIC/SHARED × profile=default/zero 四个构建目录，
#     验证 selected target/身份宏/绑定表/产物一致性、零插件产品、改坏证据的反向编译、
#     profile 切换与分配桩替换符号；
#   - 本构建目录本身不需要 ID_ASSEMBLY_PROFILE，也不会改变任何产品目标；
#   - 不修改 Assemble/**、ID/**：反向用例全部用"复制生成的证据头 + 独立 TU"的方式构造，
#     被改坏的是本驱动器 work 目录里的副本，仓库里的生成器与模板一字未动。
# =============================================================================
if(NOT ID_BUILD_VALIDATION_ASSEMBLY)
    return()
endif()

set(IDASM_DIR          ${CMAKE_CURRENT_SOURCE_DIR})
set(IDASM_WORK_DIR     ${CMAKE_CURRENT_BINARY_DIR}/work)
set(IDASM_PROBE_SOURCE ${IDASM_DIR}/src/EvidenceCategoryProbe.cpp)

if(NOT EXISTS "${IDASM_PROBE_SOURCE}")
    message(FATAL_ERROR "组装层校验缺少探针源文件：${IDASM_PROBE_SOURCE}")
endif()
if(NOT EXISTS "${CMAKE_CXX_COMPILER}")
    message(FATAL_ERROR "组装层校验需要可用的 CXX 编译器路径，当前为：${CMAKE_CXX_COMPILER}")
endif()

# 原始日志按构建目录名派生：与 P0 prototype / P1 production / P3 runtime 都不互相覆盖
get_filename_component(IDASM_BUILD_DIR_NAME "${CMAKE_BINARY_DIR}" NAME)
string(REGEX REPLACE "^cmake-build-" "" IDASM_RAW_LOG_MODE "${IDASM_BUILD_DIR_NAME}")
if(IDASM_RAW_LOG_MODE STREQUAL "")
    set(IDASM_RAW_LOG_MODE "unnamed-build")
endif()
set(IDASM_RAW_LOG_DIR ${IDASM_DIR}/../baseline/raw/assembly-${IDASM_RAW_LOG_MODE})
file(MAKE_DIRECTORY ${IDASM_RAW_LOG_DIR})
message(STATUS "assembly 校验工作目录：${IDASM_WORK_DIR}（构建目录 ${CMAKE_BINARY_DIR}）")
message(STATUS "assembly 校验原始日志目录：${IDASM_RAW_LOG_DIR}（不覆盖 ${BENCH_DIR}/report.txt）")

# 本驱动器会自己配置嵌套构建目录：这里只做参数透传，不参与本目录的目标图
add_custom_target(IDAssemblyValidationRun
    COMMAND ${CMAKE_COMMAND}
        "-DENGINE_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
        "-DWORK_DIR=${IDASM_WORK_DIR}"
        "-DRAW_LOG_DIR=${IDASM_RAW_LOG_DIR}"
        "-DRAW_LOG_MODE=assembly-${IDASM_RAW_LOG_MODE}"
        "-DGENERATOR=${CMAKE_GENERATOR}"
        "-DBUILD_TYPE=${CMAKE_BUILD_TYPE}"
        "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
        "-DCXX_COMPILER_ID=${CMAKE_CXX_COMPILER_ID}"
        "-DNM_TOOL=${CMAKE_NM}"
        "-DPROBE_SOURCE=${IDASM_PROBE_SOURCE}"
        -P ${IDASM_DIR}/cmake/RunAssemblyValidation.cmake
    USES_TERMINAL
    VERBATIM
    COMMENT "assembly validation: configure/build STATIC+SHARED x default+zero, compare evidence, corrupt-evidence reverse compiles, profile switch, stub/symbol checks")
