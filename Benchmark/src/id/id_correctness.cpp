#include "benchmark_id.hpp"

#include "benchmark_support.hpp"

/**
 *  ID 域正确性入口（工具全域）：只做子域编排与口径声明，不含具体断言。
 *
 *  - 已实现子域：`id.cmake`（CMake 描述模型）、`id.cli`（命令行）与 `id.project`（项目流程）。三个子域的
 *    用例入口各在 `id_cmake_correctness.cpp` / `id_cli_cases.cpp` / `id_project_correctness.cpp`，各子域
 *    内部自包含（自建模型、自建沙箱或假引擎根），因此这里的调用顺序不构成隐式状态依赖；用例名以
 *    `id.cmake.` / `id.cli.` / `id.project.` 前缀区分。
 *  - 预留子域：`id.json`、`id.resolve`、`id.generate` 未实现，本域不为它们设用例，也不记
 *    `Skip` 或 `Uncovered`（两者都会把"还没实现"写成"未选中"或"有客观障碍"）。这条口径同时写在
 *    `include/benchmark_id.hpp` 的域声明注释里，由本文件在报告里再声明一次以便逐次运行可核对。
 */

namespace ID::Bench
{
    void IdCorrectnessTest() noexcept
    {
        ReportLine("config (ID): 域 = ID 工具全域；已实现子域 = id.cmake（CMake 描述模型）、id.cli（命令行）与 "
                   "id.project（项目流程：new/update/build/clean/recover/list 与四类记录）；"
                   "用例名以子域前缀区分（id.cmake.* / id.cli.* / id.project.*）");
        ReportLine("config (id.project): 夹具根 = BENCHMARK_TEMP_DIR/id.project/<case>/{engine,project}"
                   "（假引擎根含 ID/ 与 IDPlugin/，项目根不落在工程根内）；真实构建统一 MinGW profile，"
                   "产物断言 <项目根>/bin/mingw/x64/<config>/；Abort 注入走子进程（期望码 124）");
        ReportLine("config (ID): 预留未实现子域 = id.json（JSON 输入）、id.resolve（依赖解析与 resolved 记录）、"
                   "id.generate（项目生成接线）——不设用例，不记 Skip，也不记 Uncovered");
        ReportLine("config (ID): 取证边界 = 工具的可观察行为（模型/文本/诊断、命令行用法与退出码、生成与 --check "
                   "幂等）；工具自身耗时只在性能区按 ungraded 记录一次，不代表游戏性能");

        IdCmakeCorrectnessTest();
        IdCliCorrectnessTest();
        IdProjectCorrectnessTest();
    }
} // namespace ID::Bench
