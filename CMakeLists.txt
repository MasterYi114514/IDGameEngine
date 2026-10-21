cmake_minimum_required(VERSION 3.21)   # CMakePresets.json 使用 schema 3 的下限

project(IDGameEngine
    LANGUAGES CXX
    VERSION 0.0.1
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译数据库：给 IDE（VS Code + CMake Tools / clangd）提供每个 TU 的真实编译命令，
# 只生成 compile_commands.json，不改变任何产物、标准或插件选择
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)

# 五层平级架构：ID（微内核）/ IDPlugin（插件）/ Sandbox（游戏本体）/ Benchmark（测试）/ Assemble（组装）
#
# 构建设置分两类，禁止互相竞争：
#   1. 产品选择：只有 ID_ASSEMBLY_PROFILE 一个开关（计划书 13.1），它决定选中插件集合；
#   2. 构建内容与模式：下面的 BOOL 开关只决定"构建什么、怎么链接"，不参与插件选择。

# ---- 1. 产品选择（唯一入口） ------------------------------------------------
# 空 = 不启用组装：只有内核与分配桩可构建（宿主/组装库/安装导出都不存在）。
# 非空 = Assemble/profiles/<名字>.json；一个 build directory 只允许一个 profile。
# 禁止用一堆独立 BOOL 开关与 profile 竞争最终插件集合。
set(ID_ASSEMBLY_PROFILE "" CACHE STRING "启用的产品组装 profile 名（Assemble/profiles/<name>.json）；为空表示不启用组装")

# ---- 2. 构建内容与模式 ------------------------------------------------------
option(ID_MONOLITHIC_BUILD    "静态归并所有插件（STATIC + 可支持的 IPO/LTO）；OFF = SHARED 内核与插件" OFF)
option(ID_ENABLE_AVX2         "开启 AVX2/FMA SIMD 指令集优化" ON)
option(ID_BUILD_SANDBOX       "构建游戏本体宿主（只经 IDGameEngine 消费产品：必须同时设置 ID_ASSEMBLY_PROFILE；默认不构建）" OFF)
option(ID_BUILD_BENCHMARK     "构建测试与性能基准" ON)
option(ID_BUILD_ASSEMBLY_SMOKE "构建组装层最小消费端冒烟（验证资产；ship 交付时关闭）" ON)

# 性能/诊断编译参数与 IPO 探测全部归位到 cmake/IDFlags.cmake，不混入插件选择逻辑
include(cmake/IDFlags.cmake)

# ---- 3. 输出目录：按 build directory + 构建模式隔离（计划书 13.2） -----------
# 旧行为是所有构建共用 <engine>/bin：SHARED 与 STATIC 的同名 ID.dll/ID.lib 会互相覆盖，
# 而且一个模式下的 dllimport 头配上另一个模式下的产物会直接链接失败。
# 现在产物归本 build directory，并按模式分子目录；多配置生成器再追加 <Config>/。
if(ID_MONOLITHIC_BUILD)
    set(ID_BUILD_MODE "static")
else()
    set(ID_BUILD_MODE "shared")
endif()
set(ID_BUILD_OUTPUT_ROOT "${CMAKE_BINARY_DIR}/out/${ID_BUILD_MODE}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${ID_BUILD_OUTPUT_ROOT}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${ID_BUILD_OUTPUT_ROOT}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${ID_BUILD_OUTPUT_ROOT}/bin")

# ---- 4. 五层编排 -----------------------------------------------------------
# 顺序固定为 内核 -> 分配桩 -> 产品组装 -> 宿主 -> 测试：宿主只消费 IDGameEngine，
# 因此组装层必须先于 Sandbox 处理，否则消费目标还不存在。
add_subdirectory(ID)
add_subdirectory(IDPlugin)

if(ID_ASSEMBLY_PROFILE)
    add_subdirectory(Assemble)
endif()

# Sandbox 的唯一前置条件是产品消费目标 IDGameEngine（由 Assemble 创建），因此这里只有两种结局：
#   - 明确要求宿主（ID_BUILD_SANDBOX=ON / 任意真值）但没有 profile：进入 Sandbox/CMakeLists.txt 的
#     配置期门卫，给出可操作错误并终止配置，不退化成"打一行 STATUS 继续"；
#   - 未要求宿主（默认 OFF）：跳过，但必须打印原因，让"没有 Sandbox 目标"可解释。
if(ID_BUILD_SANDBOX)
    add_subdirectory(Sandbox)
elseif(ID_ASSEMBLY_PROFILE)
    message(STATUS "ID: ID_BUILD_SANDBOX=OFF，跳过 Sandbox（产品已组装；需要宿主时加 -DID_BUILD_SANDBOX=ON）")
else()
    message(STATUS "ID: 未设置 ID_ASSEMBLY_PROFILE 且 ID_BUILD_SANDBOX=OFF，跳过 Sandbox"
                   "（宿主只经 IDGameEngine 消费产品；需要宿主时同时设置 profile 与 -DID_BUILD_SANDBOX=ON）")
endif()

if(ID_BUILD_BENCHMARK)
    add_subdirectory(Benchmark)   # 测试与基准
endif()

# ---- 5. 安装与导出：必须等全部选中插件目标创建之后再登记（计划书 13.4） ------
if(ID_ASSEMBLY_PROFILE)
    include(cmake/IDInstall.cmake)
endif()

# ---- 6. 构建元数据：配置名 / 模式 / assembly / 编译器在同一 build directory 内可追踪 --
# 放在最后：assembly 名字来自 Assemble 的解析结果，IPO 状态也在目标创建时才探测完成。
set(ID_INFO_ASSEMBLY "<未启用组装>")
if(ID_ASSEMBLY_PROFILE)
    get_property(ID_INFO_ASSEMBLY GLOBAL PROPERTY ID_PRODUCT_ASSEMBLY_NAME)
    if(NOT ID_INFO_ASSEMBLY)
        set(ID_INFO_ASSEMBLY "<未知：Assemble 未登记产品名>")
    endif()
endif()
if(NOT DEFINED ID_IPO_SUPPORTED)
    set(ID_IPO_SUPPORTED "false")
endif()
configure_file(cmake/IDBuildInfo.txt.in "${CMAKE_BINARY_DIR}/IDBuildInfo.txt" @ONLY)
