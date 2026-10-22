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

# =============================================================================
# 组装 fixture 矩阵（configure 期正反判定 + 编译期静态门禁 + 等价重排指纹）
#
#   - fixtures/<case>/ 每个目录是一份独立 fixture：`plugin.json`（清单）+ 最小 CMake 探针工程；
#   - cmake/RunFixtureMatrix.cmake 用一个**临时 profile** 逐个驱动它们：探针内部 include()
#     复用 Assemble/cmake 的 IDReadCatalog / IDResolveSelection / IDGenerateAssembly，
#     本目录不复制任何解析逻辑，Assemble 也不反向依赖 Benchmark（依赖方向只有 Benchmark -> Assemble）；
#   - 与上面的 IDAssemblyValidationRun 互补：那个批次做真实 target/产物/运行期取证；
#     本目标只做 configure 期语义判定与编译期静态门禁，不创建插件 target、不产出二进制；
#   - 报告、机器可读记录与原始日志**只写本构建目录**
#     （<build>/Benchmark/validation/assembly/out/），运行期不再写 validation/baseline/raw/**：
#     baseline/raw 下的历史目录只作既往证据保留，不删除也不再新增写入；**不写**官方 Benchmark/report.txt；
#   - 入口是 cmake/RunFixtureMatrixEntry.cmake：它转发参数给驱动器、捕获驱动器的**外部退出码**、
#     回填到报告头并落 out/fixture-matrix-driver-exit.txt（报告不能自证自己的外部退出码）；
#   - 四配置一致性不靠任何单独一次运行：四个配置各跑一次本目标后，用
#       cmake "-DSOURCES=label|cases|meta|exit|build_dir;..." "-DOUT=<...>/out/fixture-matrix-four-config.txt" \
#             -P <本目录>/cmake/CompareFixtureMatrixRuns.cmake
#     汇总逐 case 对照（指纹 / bindings / edges / 类别），任何差异都会被列成"必须解释"。
if(NOT EXISTS "${IDASM_DIR}/cmake/RunFixtureMatrix.cmake")
    message(FATAL_ERROR "组装 fixture 矩阵缺少驱动器：${IDASM_DIR}/cmake/RunFixtureMatrix.cmake")
endif()
if(NOT EXISTS "${IDASM_DIR}/cmake/RunFixtureMatrixEntry.cmake")
    message(FATAL_ERROR "组装 fixture 矩阵缺少入口包装：${IDASM_DIR}/cmake/RunFixtureMatrixEntry.cmake")
endif()

set(IDASM_FIXTURE_WORK_DIR   ${CMAKE_CURRENT_BINARY_DIR}/work-fixtures)
set(IDASM_FIXTURE_REPORT_DIR ${CMAKE_CURRENT_BINARY_DIR}/out)
set(IDASM_FIXTURE_RAW_DIR    ${IDASM_FIXTURE_REPORT_DIR}/raw/fixture-matrix-${IDASM_RAW_LOG_MODE})

message(STATUS "assembly fixture 矩阵工作目录：${IDASM_FIXTURE_WORK_DIR}")
message(STATUS "assembly fixture 矩阵报告目录：${IDASM_FIXTURE_REPORT_DIR}（不写 ${BENCH_DIR}/report.txt）")
message(STATUS "assembly fixture 矩阵原始日志目录：${IDASM_FIXTURE_RAW_DIR}（在构建目录内）")

# 透传给内层驱动器的参数：显式列成一份列表，入口包装原样转发（不在这里写两遍）
set(IDASM_FIXTURE_DRIVER_ARGS
    "-DASSEMBLY_DIR=${IDASM_DIR}"
    "-DENGINE_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
    "-DWORK_DIR=${IDASM_FIXTURE_WORK_DIR}"
    "-DREPORT_DIR=${IDASM_FIXTURE_REPORT_DIR}"
    "-DRAW_LOG_DIR=${IDASM_FIXTURE_RAW_DIR}"
    "-DGENERATOR=${CMAKE_GENERATOR}"
    "-DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
    "-DGENERATOR_INSTANCE=${CMAKE_GENERATOR_INSTANCE}"
    "-DBUILD_TYPE=${CMAKE_BUILD_TYPE}"
    "-DRUN_CONFIG=$<CONFIG>"
    "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
    "-DCXX_COMPILER_ID=${CMAKE_CXX_COMPILER_ID}")

add_custom_target(IDAssemblyFixtureMatrixRun
    COMMAND ${CMAKE_COMMAND}
        "-DASSEMBLY_DIR=${IDASM_DIR}"
        "-DREPORT_DIR=${IDASM_FIXTURE_REPORT_DIR}"
        "-DIDASM_DRIVER_ARGS=${IDASM_FIXTURE_DRIVER_ARGS}"
        -P ${IDASM_DIR}/cmake/RunFixtureMatrixEntry.cmake
    USES_TERMINAL
    VERBATIM
    COMMENT "assembly fixture matrix: per-case configure (include() reused Assemble parsers) + generated-artifact assertions + compile-time static gates + reorder fingerprint equality (driver external exit code backfilled)")

# =============================================================================
# 默认 OFF 回归（本资产默认不参与构建）
#
#   - 四个配置各用一个独立构建目录 configure，开关显式给 OFF、profile 显式给空；
#   - 判据：configure 退出码 0 + CMakeCache 里开关确实是 OFF + 构建目录里 0 条 IDAssembly* 路径
#     + 空 profile 时没有 <build>/Assemble/ 子目录；判定器先用**当前 ON 构建目录**做正向自检；
#   - 报告写本构建目录（out/validation-default-off.txt），不写官方 report.txt。
# =============================================================================
if(NOT EXISTS "${IDASM_DIR}/cmake/CheckValidationDefaultOff.cmake")
    message(FATAL_ERROR "默认 OFF 回归缺少脚本：${IDASM_DIR}/cmake/CheckValidationDefaultOff.cmake")
endif()

set(IDASM_OFF_VARIANTS
    "mingw-debug|${BENCH_DIR}/build/validation-off-mingw-debug|MinGW Makefiles|n/a|n/a|Debug"
    "mingw-release|${BENCH_DIR}/build/validation-off-mingw-release|MinGW Makefiles|n/a|n/a|Release"
    "msvc-debug|${BENCH_DIR}/build/validation-off-msvc-debug|Visual Studio 18 2026|x64|${CMAKE_GENERATOR_INSTANCE}|n/a"
    "msvc-release|${BENCH_DIR}/build/validation-off-msvc-release|Visual Studio 18 2026|x64|${CMAKE_GENERATOR_INSTANCE}|n/a")

# 自检控制组：当前构建目录 + 规范 MinGW 构建目录，覆盖两种生成器的目标痕迹形式
set(IDASM_OFF_CONTROLS "${CMAKE_BINARY_DIR};${BENCH_DIR}/build/assembly-fixtures")

add_custom_target(IDAssemblyDefaultOffRegression
    COMMAND ${CMAKE_COMMAND}
        "-DBENCHMARK_DIR=${BENCH_DIR}"
        "-DOUT_DIR=${IDASM_FIXTURE_REPORT_DIR}"
        "-DCONTROL_BUILD_DIRS=${IDASM_OFF_CONTROLS}"
        "-DVARIANTS=${IDASM_OFF_VARIANTS}"
        -P ${IDASM_DIR}/cmake/CheckValidationDefaultOff.cmake
    USES_TERMINAL
    VERBATIM
    COMMENT "assembly validation default-off regression: 4 configurations configure with ID_BUILD_VALIDATION_ASSEMBLY=OFF and contain zero IDAssembly* targets (detector self-checked against this ON build dir)")
