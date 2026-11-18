#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <string_view>

#include "project/Cli/Report.hpp"

/**
 *  `ID project ...` 的进程内入口与命令编排（docs/id/10 §10.7、§10.8）。
 *
 *  本单元是**唯一**知道"命令 → 阶段序列"的地方：其它单元不读命令行，判定（冲突、可信性、profile）都由
 *  U5/U9 给出，这里只组合与输出。
 *
 *  不变量：
 *  1. 只读命令（`list`、`--check`）零写：不取锁、不建目录、不写日志、不分配日志序号。
 *  2. 工具日志只走 console 目标（stdout）：非构建命令不产生日志文件；`IDProject_logger_init()` 返回
 *     `false` 一律按 `write_failed`（2）退出；`IDProject_logger_shutdown()` 覆盖全部退出路径且幂等。
 *  3. 退出码只由 `select_exit_code(diagnostics, has_plan_diff)` 决定；差异判定只在本层，且只对
 *     `new`/`update`/`clean` 传 `has_plan_diff`，`build` 与 `recover` 恒传真。
 *  4. `--check`/`--dry-run` 是同一语义的两个别名，五个子命令都接受。
 *  5. `run_command_line` 不依赖除注入配置与日志器之外的全局可变状态：同一进程内可多次调用（测试用途）。
 *  6. `--check` 的每个退出分支（P0 失败、profile 失败、组装失败、计划无效、收敛失败、正常结束）都经
 *     `finish_check_with` 复验一次只读快照：结论出自读取时的状态，复验失败按 `snapshot_unstable`（2）
 *     落在同一套诊断里，并且不输出基于过期快照的清单。
 */

namespace ID::Project
{
    /**
     *  一次命令的运行上下文：引擎根、登记路径、插件源根与输出通道。
     */
    struct RunnerContext
    {
        std::filesystem::path   engine_root                  = {};   // `Paths.IDPath` 的父目录
        std::filesystem::path   registry_path                = {};
        std::filesystem::path   plugin_root                  = {};
        CommandIo               io                           = {};
        bool                    is_injection_initialized     = false;   // 调用方已用 init_injection 读过环境
    };

    /**
     *  @brief 进程内入口：`ID project ...` 的完整实现
     *  @param arguments `project` 之后的全部参数
     *  @param context 运行上下文
     *  @return 退出码（spec §13.1）
     *  @note 供 `main.cpp` 与 `Benchmark` 共同使用：测试可直接调用并断言退出码，不经进程启动开销；
     *        崩溃类用例必须用子进程（注入点 Abort 会终止调用方进程）
     *  @note `context.is_injection_initialized` 为假时本函数自己读一次 `ID_TEST_INJECT`，因此进程内驱动
     *        不需要额外的初始化调用
     */
    int run_command_line(std::span<const std::string> arguments, const RunnerContext& context);

    /**
     *  @brief 引擎根的解析：即 `Paths.IDPath` 的父目录
     *  @param override_root 显式覆盖（空表示未给）
     *  @return 解析结果；解析不到合格目录时返回空路径
     *  @note 合格目录的判据：必须含 `ID/`，且含 `IDPlugin/` 或已经存在 `ID/project.json` 本机登记。
     *        后一种让"源插件库整体不可用"时仍能定位项目：`build`/`recover`/`clean`/`list` 不读源插件库
     *        （spec §8.3、§12.1）；`new`/`update` 真缺 `IDPlugin/` 由 P0 的"可读的插件目录"报 `project_path_invalid`
     *  @note 解析顺序（唯一口径，不读工作台配置——配置文件属于工作台，不是工程）：
     *        1. `override_root` 非空则用它（必须是绝对路径）
     *        2. 环境变量 `ID_ENGINE_ROOT`（供测试与多工程并存时使用）
     *        3. 可执行文件所在目录名为 `bin` 时取其父目录（`ID` 的产物目录约定）
     *        4. 当前工作目录
     *  @note 失败诊断由调用方给出：本函数返回空路径，`run_command_line` 报 `project_path_invalid`（1）
     *        并在 `actual` 里列出实际取值与已尝试的来源
     */
    std::filesystem::path resolve_engine_root(std::string_view override_root);
} // namespace ID::Project
