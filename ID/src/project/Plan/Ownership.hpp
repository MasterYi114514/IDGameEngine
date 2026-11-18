#pragma once

#include <cstdint>
#include <optional>

#include "project/Base/Digest.hpp"
#include "project/Record/IntentRecord.hpp"

/**
 *  所有权判定与恢复三态判定：`(path, fragment)` 粒度上的纯判定，不读盘、不写盘、不取锁。
 *
 *  判定单位是 `(path, fragment)`：普通文件 `fragment` 为空、摘要对象是整文件字节；受控片段
 *  `fragment` 非空、摘要对象是该片段的文本范围（含 BEGIN/END 标记行）。同一片段的 D、O、N 一律取这一
 *  范围，不允许一处用整文件摘要、另一处用片段摘要。
 *
 *  不变量：
 *  1. `judge_ownership` 的判定顺序固定：先归属、后摘要。`D` 有而 `O` 无即 `BlockUnregistered`，
 *     与 `N` 及 `--force` 无关；`--force` 不构成归属证明。
 *  2. `D = N`（归属可证）先于 `D = O` 判定：它表达"本 attempt 上次已写对"或"内容本就相同"，
 *     不因 `D ≠ O` 报冲突。
 *  3. `D = O` 且 `N ≠ O` 是正常内容升级，默认允许原子刷新，不需要 `--force`。
 *  4. `judge_three_state` 只对 `AddressScope::Project` 的条目调用：`Engine` 作用域的登记写入不是
 *     `(path, fragment)` 条目。
 *  5. 本单元无副作用：不读磁盘、不配对日志、不产出诊断。
 */

namespace ID::Project
{
    /**
     *  所有权判定结论。取值覆盖写入方向、删除方向与"无变化"三类。
     */
    enum class OwnershipVerdict : std::uint8_t
    {
        // 写入方向
        CreateNew          = 0,    // D absent、O 无、N 有
        RefreshIdentical   = 1,    // D = N、归属可证：跳过内容写入，按需更新记录
        UpgradeManaged     = 2,    // D = O、N ≠ O：正常内容升级，允许默认原子刷新
        RestoreMissing     = 3,    // D absent、O 有、N 有：update 重建（before = absent）
        BlockModifiedLocal = 4,    // D ≠ O、D ≠ N、未给 --force
        OverwriteForced    = 5,    // 同上但给了 --force：先完整备份再覆盖
        BlockUnregistered  = 6,    // D 存在、O 无：未登记路径，--force 也不放行
        // 删除方向（N absent）
        DeleteAllowed      = 7,    // D = O：默认删除
        DeleteNeedsForce   = 8,    // D ≠ O、未给 --force：cleanup_blocked
        DeleteForced       = 9,    // D ≠ O、给了 --force：先备份再删
        DeleteAlreadyGone  = 10,   // D absent、O 有、N absent：跳过
        // 无变化
        NoChange           = 11,   // N = O 且 D = O：既不改内容也不改记录
    };

    /**
     *  一次判定的三份摘要。`absent` 用空 optional 表达：D 无、O 无（未登记）、N 无（裁剪项）。
     */
    struct OwnershipInput
    {
        std::optional<Digest> disk       = {};   // D：当前磁盘上的对应对象摘要
        std::optional<Digest> record     = {};   // O：已提交 manifest 为该身份登记的摘要
        std::optional<Digest> planned    = {};   // N：本次计划要写出的摘要
        bool                  has_force  = false;
    };

    /**
     *  @brief 按 spec §1.3 的判定表给唯一结论
     *  @param input 三份摘要与 --force
     *  @return 唯一的判定结论
     *  @note 判定顺序固定（先归属、后摘要），不得调换：
     *        1. `D` 有、`O` 无 → `BlockUnregistered`（无论 N、无论 force）
     *        2. `N` 无（裁剪项）：`D` 无 → `DeleteAlreadyGone`；`D = O` → `DeleteAllowed`；
     *           `D ≠ O` → force ? `DeleteForced` : `DeleteNeedsForce`
     *        3. `N` 有：`D` 无 → `O` 无 ? `CreateNew` : `RestoreMissing`
     *        4. `D = N` → `RefreshIdentical`（不看 O）
     *        5. `D = O` → `N = O` ? `NoChange` : `UpgradeManaged`
     *        6. 其余 → force ? `OverwriteForced` : `BlockModifiedLocal`
     *  @note `D = N` 且 `O` 存在的分支必须先于 `D = O` 判定：它表达"本 attempt 上次已写对"，
     *        不因 `D ≠ O` 报冲突
     */
    OwnershipVerdict judge_ownership(const OwnershipInput& input) noexcept;

    /**
     *  恢复三态：当前对象相对该项 `before` / `after` 摘要的关系。
     */
    enum class ThreeState : std::uint8_t
    {
        Before = 0,   // 未执行或已撤销
        After  = 1,   // 已执行
        Other  = 2,   // 第三种内容：整个 restore 在预检阶段终止
    };

    /**
     *  @brief 三态判定（恢复用）
     *  @param current 当前对象摘要；`absent` 用空 optional 表示
     *  @param before 该项记录的 before 摘要
     *  @param after 该项记录的 after 摘要
     *  @return `Before`（当前等于 before，含 `before = after` 的 no-op 项）、`After`（当前等于 after）、
     *          或 `Other`
     *  @note 先判 `before` 再判 `after`：`before = after` 的项因此落在 `Before`（跳过、不读 payload）
     *  @note `absent` 是明确的第三取值：`before` 与 `after` 同时为 `absent` 时"两者相等"，按 `Before` 处理
     */
    ThreeState judge_three_state(std::optional<Digest> current, const DigestState& before,
                                 const DigestState& after) noexcept;

    /**
     *  @brief 把记录里的摘要状态转成可比较的摘要
     *  @param state 记录摘要状态（`absent` 或 64 位十六进制）
     *  @param out 解析结果；`absent` 时置空，成功时被覆盖
     *  @return `absent`、或十六进制合法时返回 true；十六进制非法返回 false（记录不可信）
     */
    bool digest_state_value(const DigestState& state, std::optional<Digest>& out) noexcept;
} // namespace ID::Project
