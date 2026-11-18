#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "project/Apply/Session.hpp"
#include "project/Base/Diagnostic.hpp"
#include "project/Base/Error.hpp"
#include "project/Plan/PlanTypes.hpp"

/**
 *  动作执行：把已通过校验的计划落成磁盘状态（P2–P6）。
 *
 *  本单元**不判定**冲突、选择与可信性：计划已给出动作序列与冲突集合，本单元只按顺序落盘并在失败处停下；
 *  也不重新计算摘要或换一份保留集——D 与计划不一致时报 `changed_since_plan`（2）。
 *
 *  不变量：
 *  1. 写 `intent` 之前先做锁内基线复验：复读两份记录字节摘要与会话基线比较（不存在的一侧按 absent 比较），
 *     不一致即 `changed_since_plan`（2）、**不写 intent**、零写退出，`--force` 不豁免。
 *  2. 提交点唯一：`resolved.json` 的原子替换。`clean` 共用同一提交点。
 *  3. 每个受管文件与片段在变更前都已落 before-payload 并回读校验；payload 失败即停。
 *  4. 片段写入走 `write_managed_blocks`，写后用 `read_fragments` 逐片段复验，摘要只经
 *     `fragment_range_digest` 计算。
 *  5. 提交点之后的三段失败预期（manifest 未替换 / resolved 已替换而 intent 未删 / intent 已删而标记未写）
 *     逐条区分，不出现"声称 intent 仍在"的分支。
 *  6. `is_noop` 的计划不写 `intent`：只执行 Engine 作用域的登记动作（spec §8.9 的标记修复路径）。
 */

namespace ID::Project
{
    /**
     *  执行选项。
     */
    struct RunOptions
    {
        CommandKind              command        = CommandKind::Update;
        bool                     has_force      = false;
        bool                     is_build_after = false;   // 命令带 --build；本单元不驱动构建
        std::vector<std::string> next_argv      = {};      // 收敛用的命令行（不含程序名）
        std::filesystem::path    registry_path  = {};
        std::string              project_name   = {};
    };

    /**
     *  执行结果：`exit_code` 由 `select_exit_code` 给出，`is_intent_kept` 表达"intent 仍在磁盘上"。
     */
    struct RunResult
    {
        int           exit_code     = kExitSuccess;
        bool          is_committed  = false;
        bool          is_intent_kept = false;
        DiagnosticSet diagnostics   = {};
    };

    /**
     *  @brief 执行 plan：P2（仅 new 的登记与首次选择）→ P3 → P4 → P5 → P6
     *  @param session 已打开的项目会话（持项目锁）
     *  @param plan 已通过校验的 plan（`is_valid` 为真）
     *  @param options 运行选项
     *  @return 退出码、提交事实、intent 是否保留与诊断
     *  @note 固定顺序：锁内基线复验 → 写 intent → （仅 new）登记与首次 `plugins.json` → 建目录 → 逐项
     *        before-payload → 备份 → 内容写入与复制 → 裁剪 → manifest → resolved（提交点）→ 删 intent 与
     *        payload → 登记标记
     *  @note 任一步失败：保留 intent 与 payload，按失败类别给码，不抹此前的已提交代
     *  @note `is_noop` 的计划只执行 Engine 动作（标记重写），不写 intent、不改两份记录
     */
    RunResult execute_plan(Session& session, const ProjectPlan& plan, const RunOptions& options);
} // namespace ID::Project
