#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "project/Apply/Session.hpp"
#include "project/Base/Diagnostic.hpp"
#include "project/Base/Error.hpp"
#include "project/Plan/Ownership.hpp"
#include "project/Record/IntentRecord.hpp"

/**
 *  `recover`：收敛未完成的 attempt（`continue` 续做提交与收尾 / `restore` 按 before-payload 撤销）。
 *
 *  本单元**不接收 plan、不重跑 planner、不生成文本、不读源插件库**：依据只有 `intent` 的快照、逐项
 *  `before`/`after` 摘要、before-payload 字节与当前磁盘（spec §6.6）。结构上就不可能重新规划。
 *
 *  不变量：
 *  1. 判定顺序固定：先判"已提交窗口"（`resolved` 当前摘要等于 `intent.target_resolved.digest`），
 *     成立时 `continue` 只收尾、`restore` 报 `recovery_conflict`；不成立才判 `continue` 的其余前置
 *     （`selection_digest` 一致、所有内容项当前等于 `after`）。顺序颠倒会让输入已变的已提交窗口被误报。
 *  2. `recover` 是唯一的跨命令收敛入口，不比较 `intent.command`；原命令路径的比较由调用方负责。
 *  3. `restore` 预检阶段零写：逐项三态，任一 `Other` 或 payload 缺失/损坏即 `recovery_conflict`（2）并终止。
 *  4. `restore` 只撤销本 attempt 对工具 owned 路径与受控片段的改动：`plugins.json`、`src/` 子树（含 seed）、
 *     根 `CMakeLists.txt` 本体与片段外字节一律保留；片段按其文本范围处理，不整文件回滚。
 *  5. `AddressScope::Engine` 的登记写入不参与三态判定，只按 base 快照重写登记值或置回 `false`。
 */

namespace ID::Project
{
    /**
     *  收敛动作。
     */
    enum class RecoveryAction : std::uint8_t
    {
        Continue = 0,
        Restore  = 1,
    };

    /**
     *  收敛选项。
     */
    struct RecoveryOptions
    {
        RecoveryAction        action        = RecoveryAction::Continue;
        bool                  is_check      = false;
        std::filesystem::path registry_path = {};
        std::string           project_name  = {};
    };

    /**
     *  一个内容项的收敛计划：三态结论与将执行的动作。
     */
    struct RecoveryItem
    {
        std::string                operation_id = {};
        std::string                path         = {};
        std::optional<std::string> fragment     = {};
        ThreeState                 state        = ThreeState::Other;
        std::string                planned      = {};   // "continue" / "restore" / "skip"
    };

    /**
     *  收敛结果。
     */
    struct RecoveryResult
    {
        int                       exit_code    = kExitSuccess;
        bool                      is_committed = false;
        std::vector<RecoveryItem> items        = {};
        DiagnosticSet             diagnostics  = {};
    };

    /**
     *  @brief 收敛未完成的 attempt
     *  @param session 已打开的项目会话（持项目锁）；`--check` 时可为 `nullptr`
     *  @param project_root 项目根绝对路径（`--check` 不依赖会话，路径由调用方给出）
     *  @param intent_path `<项目根>/IDGenerated/intent.json`
     *  @param intent 已解析的 attempt
     *  @param options 动作与 `--check`
     *  @return 退出码、逐项结论与诊断
     *  @note `continue`：内容已全部落盘时只走提交点与收尾（manifest → resolved → 删 intent 与 payload →
     *        按需重写标记），不逐项重做内容；前置不满足即 `recovery_conflict`（2）、零写，并在诊断里给
     *        `intent.next_argv` 的原命令与 `recover <name> --action restore` 两条可复制命令
     *  @note `restore`：预检零写；写回顺序为内容 → 目录回退（`created_dirs` 逆序、只删空目录）→ 记录；
     *        写回前按需补建目标缺失的父目录（裁剪阶段可能已删掉该目录），只建不删、不按通配符；
     *        首次 `new` 无 base 成功代时删除两条记录并把登记置回 `false`
     *  @note `--check` 只输出逐项结论，不写盘：不取锁、不建目录、不写记录与临时文件，因此不需要会话
     *  @note 写路径要求 `session` 非空且已打开；否则按 `write_failed`（2）报实现缺陷，不半途写入
     */
    RecoveryResult recover(Session* session, const std::filesystem::path& project_root,
                           const std::filesystem::path& intent_path, const IntentRecord& intent,
                           const RecoveryOptions& options);
} // namespace ID::Project
