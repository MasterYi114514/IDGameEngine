#pragma once

/**
 *  ID 测试域声明（工具全域，只有该域的函数声明，不含实现）。
 *
 *  子域划分与登记（域/子域清单的唯一正文见 skills/benchmark/references/framework.md 第 1 节）：
 *  - `id.cmake`：`ID/src/cmake/**` 的 CMake 描述模型——值层（`Argument`）、文本层（`Writer`）、
 *    目标层（`Target` + `emit_target`）、目录层（`Directory` + `emit_directory`）、工程层（`Project` +
 *    `collect_outputs`）、生成前校验（`validate`）与落盘层（`FragmentWriter`）。以普通静态库方式消费
 *    `IDCore`：经它的 PUBLIC include 根取到 `cmake/...` 头，不经 `ID` 的生成接线、无运行期服务槽。
 *    用例在 `src/id/id_cmake_*.cpp`，共享辅助在私有头 `src/id/id_cmake_test_support.hpp`。
 *  - `id.cli`：工具可执行文件的命令行（`ID cmake self --root <工程根> [--check]`）——用法错误一律退出码 3、
 *    生成 0 且逐文件写 `written`、再生成幂等 `unchanged`、`--check` 一致 0 / 不一致 1（逐文件报类别）、
 *    写入失败 2。用例在 `src/id/id_cli_cases.cpp`，经构建注入的 `BENCHMARK_ID_EXE` 以子进程调用被测工具，
 *    不在用例里写死可执行文件路径。
 *  - `id.project`：`ID project` 的六条子命令（`new` / `update` / `build` / `clean` / `recover` / `list`）
 *    与四类记录（`project.json` / `plugins.json` / `resolved.json` / `manifest.json`，以及 `intent.json` /
 *    `build.json`）。用例在 `src/id/id_project_*.cpp`，共享辅助在私有头 `src/id/id_project_test_support.hpp`；
 *    夹具根一律建在 `BENCHMARK_TEMP_DIR` 下（假引擎根 + 独立项目根），真实构建共用统一 MinGW profile，
 *    异常终止类注入走子进程（`BENCHMARK_ID_EXE` + `ID_TEST_INJECT`，期望码 124）。只出正确性判定，
 *    工具耗时不是游戏性能。
 *  - `id.json` / `id.resolve` / `id.generate`：**预留、未实现**子域（JSON 输入、依赖解析与 resolved 记录、
 *    项目生成接线）。未实现期间不设用例，也**不记 `Skip` 或 `Uncovered`**——两者都会把"还没实现"写成
 *    "未选中"或"有客观障碍"；实现落地后在同一 `ID` 域内补齐子域、用例与 `Run` 登记。
 *
 *  契约源：`docs/cmake-implementation-plan.md` §5.2 / §5.3 / §5.7 / §5.8 / §5.9 / §5.11 与各公开头顶部注释。
 *
 *  工具耗时（`ID cmake` 的生成流水线）不是游戏性能：本域只记录一次并标 `ungraded`，不出评级。
 */

// id.cli 经构建期注入的工具路径调用被测可执行文件；缺失时为空串，用例按"未注入"登记 Uncovered，不猜路径
#if !defined(BENCHMARK_ID_EXE)
    #define BENCHMARK_ID_EXE ""
#endif

namespace ID::Bench
{
    /// @brief ID 域正确性入口：先 id.cmake 子域、后 id.cli 子域（两个子域各自自包含）
    void IdCorrectnessTest() noexcept;

    /// @brief ID 域性能入口：转发 id.cmake 子域的工具耗时用例（id.cli 只断言行为、不计耗时）
    void IdPerformanceTest() noexcept;

    /// @brief ID 域性能区的统计口径与覆盖局限说明（在指标表之后输出，不混入指标行）
    void IdPerformanceNotes() noexcept;

    /// @brief id.cmake 子域正确性用例（值层、文本层、目标层、目录层、工程层、validate、落盘层）
    void IdCmakeCorrectnessTest() noexcept;

    /// @brief id.cmake 子域的工具耗时用例：`validate` + `emit_directory` 整条生成流水线的单次耗时（ungraded）
    void IdCmakePerformanceTest() noexcept;

    /// @brief id.cmake 子域性能区的统计口径与覆盖局限说明
    void IdCmakePerformanceNotes() noexcept;

    /// @brief id.cli 子域正确性用例（用法与退出码、生成、`--check` 幂等与不一致）
    void IdCliCorrectnessTest() noexcept;

    /**
     *  @brief id.project 子域正确性用例（`ID project` 六条子命令与四类记录）
     *  @note 夹具根 = `<BENCHMARK_TEMP_DIR>/id.project/<case>/`，每用例独立假引擎根（`engine/` 含 `ID/` 与
     *        `IDPlugin/`）与项目根；经注入的 `ID_ENGINE_ROOT`/进程内上下文定位，不读工作台 `Paths.*`。
     *        真实构建统一 `--generator "MinGW Makefiles" --toolchain mingw --arch x64`，产物断言
     *        `<项目根>/bin/mingw/x64/<config>/`；`Abort` 注入走子进程（`BENCHMARK_ID_EXE` + `ID_TEST_INJECT`，
     *        期望码 124）。本子域只出正确性判定，工具耗时不是游戏性能。
     */
    void IdProjectCorrectnessTest() noexcept;
} // namespace ID::Bench
