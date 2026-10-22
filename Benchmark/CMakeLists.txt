cmake_minimum_required(VERSION 3.20)

set(BENCH_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# 编译期插件声明原型的入口开关：默认关闭，普通 Benchmark 构建不受影响。
# 开启后额外构建 validation/prototype 下的校验目标；反向编译用例仍全部 EXCLUDE_FROM_ALL，
# 不会被普通 src/*.cpp glob 捡到，也不会进入 Benchmark 可执行文件。
option(ID_BUILD_VALIDATION_PROTOTYPE "构建 Benchmark/validation 下的编译期校验原型" OFF)

# P1 生产 API 校验入口：默认关闭。开启后只额外构建 validation/production 的校验目标；
# 反向编译用例全部 EXCLUDE_FROM_ALL，不会被普通 src/*.cpp glob 捡到。
option(ID_BUILD_VALIDATION_PRODUCTION "构建 Benchmark/validation/production 下的生产 API 校验目标" OFF)

# P3 运行期 PluginRuntime 校验入口：默认关闭。开启后只额外构建 validation/runtime 的
# 运行期正确性可执行文件；其报告与原始日志都写本构建目录与 baseline/raw/runtime-*，
# 源文件在 validation/runtime/src 下，不会被普通 src/*.cpp glob 捡到。
option(ID_BUILD_VALIDATION_RUNTIME "构建 Benchmark/validation/runtime 下的运行期正确性校验目标" OFF)

# ID.Log 终止语义校验入口：默认关闭。开启后只额外构建 validation/log 的父/子进程校验目标
# （IDLogFatalValidation）；该目标要求 ID_ASSEMBLY_PROFILE 选中 ID.Log，源文件在
# validation/log 下，不会被普通 src/*.cpp glob 捡到，也不写官方 report.txt。
option(ID_BUILD_VALIDATION_LOG "构建 Benchmark/validation/log 下的 ID.Log 终止语义校验目标" OFF)

# P2 组装层校验入口：默认关闭。开启后只注册一个批量驱动器目标（IDAssemblyValidationRun），
# 由它在自己的 work 目录下另行配置 STATIC/SHARED × default/zero 四个构建目录并验证组装结果；
# 本构建目录本身不需要设置 ID_ASSEMBLY_PROFILE，也不会因为开启它而改变任何产品目标。
option(ID_BUILD_VALIDATION_ASSEMBLY "构建 Benchmark/validation/assembly 下的组装层校验驱动器" OFF)

# 数学域编译失败负例入口：默认关闭。开启后只注册一个驱动器目标（IDMathNegativeCompileRun），
# 它在自己的 build 目录里用本工程同一个编译器逐个编译"期望失败"的负例 TU，并断言诊断里恰好
# 一条静态断言 + 含 EXPECTED:<类别> + 不含 MISMATCH；同时先编译一份同环境的合法对照。
# 该目标不写官方 report.txt、不参与评级，也不会被 src/*.cpp 的 glob 捡进官方 exe。
option(ID_BUILD_VALIDATION_MATH "构建 Benchmark/validation/math 下的数学域编译失败负例驱动器" OFF)

# ID.Log 参考值测量阶段开关：ON 时把该域 6 条指标的 reference 强制为 0（报告按 ungraded 输出），
# 用于重新标定分配置参考值。默认 OFF；任何回归/验收构建必须保持 OFF，否则指标全部 ungraded。
option(ID_BENCH_LOG_REFERENCE_UNSET "ID.Log 指标强制未标定（reference=0/ungraded），仅用于参考值测量阶段" OFF)

# 用 GLOB_RECURSE + CONFIGURE_DEPENDS：新增测试域源文件不必手改本文件，也不会漏编译
file(GLOB_RECURSE BENCH_SRC CONFIGURE_DEPENDS ${BENCH_DIR}/src/*.cpp)

add_executable(Benchmark ${BENCH_SRC})
target_include_directories(Benchmark PRIVATE ${BENCH_DIR}/include)

# 链接口径分两种，判定条件只有"同一构建目录里目标是否已经存在"，不看 profile 名字：
#   1. 默认口径（空 profile、或选中的插件集合里既没有 IDLog 也没有数学 backend）：只链内核与分配桩，
#      测试内容不随 ID_ASSEMBLY_PROFILE 的插件选择变化；
#   2. 需要产品能力头的口径：仅当 Assemble 已创建 IDGameEngine（ID_ASSEMBLY_PROFILE 非空）**且**
#      catalog/profile 真的选中了至少一个"只在 product 侧可见"的插件 target 时才改链产品消费目标。
#      理由：ID.Log 域的 `ID_USE_PLUGIN_IDLOG` + IDLog 公开头，以及两个数学域的
#      `ID_USE_PLUGIN_IDMATHSCALAR` / `ID_USE_PLUGIN_IDMATHSIMD` + 各自公开头，都只能由对应插件
#      target 的 PUBLIC usage requirement 经 IDAssembly 传播；不允许在这里全局注入宏或 include 路径
#      （所以下面只判目标是否存在，不写任何插件名特判，也不新增 backend 开关）。
#      IDMathScalar / IDMathSIMD 是 interface 插件，两条链任意命中其一都说明本 profile 选了数学 backend。
if(TARGET IDGameEngine AND (TARGET IDLog OR TARGET IDMathScalar OR TARGET IDMathSIMD))
    target_link_libraries(Benchmark PRIVATE IDGameEngine)
else()
    target_link_libraries(Benchmark PRIVATE ID IDPluginStub)
endif()

# report.txt 的路径以编译期宏注入（源码目录的绝对路径），保证与运行时的 CWD 无关。
# ID_BENCH_LOG_TMP_DIR 同理：ID.Log 域的文件 sink / 轮转 / console 捕获临时文件一律落到本构建目录，
# 不再用相对路径（那会把 idlog_bench_file.log 落到当前工作目录，可能是仓库根）。
target_compile_definitions(Benchmark PRIVATE
    ID_BENCHMARK_REPORT_DIR="${BENCH_DIR}"
    ID_BENCH_LOG_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}"
    ID_BENCH_CONFIG="$<CONFIG>"
    ID_BENCH_LTO=$<IF:$<BOOL:${ID_MONOLITHIC_BUILD}>,1,0>
    ID_BENCH_AVX2=$<IF:$<BOOL:${ID_ENABLE_AVX2}>,1,0>
    ID_BENCH_MONOLITHIC=$<IF:$<BOOL:${ID_MONOLITHIC_BUILD}>,1,0>
)

# 测量阶段（仅重新标定参考值时开启）：让 ID.Log 域把 reference 视为 0，报告回到 ungraded；
# 默认关闭，回归/验收构建必须保持关闭
if(ID_BENCH_LOG_REFERENCE_UNSET)
    target_compile_definitions(Benchmark PRIVATE ID_BENCH_LOG_REFERENCE_UNSET=1)
endif()

if(COMMAND id_apply_performance_flags)
    id_apply_performance_flags(Benchmark)
endif()

# 校验资产（P0 原型）：显式开启才构建
if(ID_BUILD_VALIDATION_PROTOTYPE)
    add_subdirectory(${BENCH_DIR}/validation/prototype)
endif()

# P1 生产 API 校验资产（默认关闭）
if(ID_BUILD_VALIDATION_PRODUCTION)
    add_subdirectory(${BENCH_DIR}/validation/production)
endif()

# P3 运行期正确性校验资产（默认关闭）
if(ID_BUILD_VALIDATION_RUNTIME)
    add_subdirectory(${BENCH_DIR}/validation/runtime)
endif()

# P2 组装层校验资产（默认关闭）
if(ID_BUILD_VALIDATION_ASSEMBLY)
    add_subdirectory(${BENCH_DIR}/validation/assembly)
endif()

# ID.Log 终止语义校验资产（默认关闭；需要 profile 选中 ID.Log）
if(ID_BUILD_VALIDATION_LOG)
    add_subdirectory(${BENCH_DIR}/validation/log)
endif()

# 数学域编译失败负例（默认关闭；需要 profile 选中至少一个数学 backend）
if(ID_BUILD_VALIDATION_MATH)
    add_subdirectory(${BENCH_DIR}/validation/math)
endif()
