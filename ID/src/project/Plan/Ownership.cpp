#include "project/Plan/Ownership.hpp"

#include <optional>

/**
 *  七行判定表与三态判定的唯一实现。
 *
 *  判定顺序即 spec §1.3 的行序；`D = N` 必须早于 `D = O`，否则"本 attempt 已写对但记录的 O 是上一代摘要"
 *  会被误报成本地修改；`D` 有而 `O` 无必须在最前，否则未登记路径会被 `D = N` 或 `--force` 顺手接管。
 */

namespace ID::Project
{
    namespace
    {
        bool is_equal(const std::optional<Digest>& left, const std::optional<Digest>& right) noexcept
        {
            if (left.has_value() != right.has_value())
            {
                return false;
            }
            if (!left.has_value())
            {
                return true;
            }
            return left->bytes == right->bytes;
        }
    } // namespace

    OwnershipVerdict judge_ownership(const OwnershipInput& input) noexcept
    {
        // 1. 先判归属：磁盘上有对象而记录里没有它，任何摘要关系与 --force 都不构成归属证明
        if (input.disk.has_value() && !input.record.has_value())
        {
            return OwnershipVerdict::BlockUnregistered;
        }

        // 2. 删除方向：本次不产出该条目
        if (!input.planned.has_value())
        {
            if (!input.disk.has_value())
            {
                return OwnershipVerdict::DeleteAlreadyGone;
            }
            if (is_equal(input.disk, input.record))
            {
                return OwnershipVerdict::DeleteAllowed;
            }
            return input.has_force ? OwnershipVerdict::DeleteForced : OwnershipVerdict::DeleteNeedsForce;
        }

        // 3. 写入方向：磁盘上没有对象，按记录是否登记过区分新建与重建
        if (!input.disk.has_value())
        {
            return input.record.has_value() ? OwnershipVerdict::RestoreMissing : OwnershipVerdict::CreateNew;
        }

        // 4. 目标内容已在磁盘上：归 record 无关，不因 D ≠ O 报冲突
        if (is_equal(input.disk, input.planned))
        {
            return OwnershipVerdict::RefreshIdentical;
        }

        // 5. 磁盘上就是上次提交的内容：正常内容升级或完全无变化
        if (is_equal(input.disk, input.record))
        {
            return is_equal(input.planned, input.record) ? OwnershipVerdict::NoChange
                                                         : OwnershipVerdict::UpgradeManaged;
        }

        // 6. 其余：受管文件被本地修改
        return input.has_force ? OwnershipVerdict::OverwriteForced : OwnershipVerdict::BlockModifiedLocal;
    }

    ThreeState judge_three_state(std::optional<Digest> current, const DigestState& before,
                                 const DigestState& after) noexcept
    {
        std::optional<Digest> before_value;
        std::optional<Digest> after_value;
        if (!digest_state_value(before, before_value) || !digest_state_value(after, after_value))
        {
            // 记录里的摘要形态非法属记录不可信，调用方按 record_invalid 处理；此处给最保守的第三态
            return ThreeState::Other;
        }

        if (is_equal(current, before_value))
        {
            return ThreeState::Before;
        }
        if (is_equal(current, after_value))
        {
            return ThreeState::After;
        }
        return ThreeState::Other;
    }

    bool digest_state_value(const DigestState& state, std::optional<Digest>& out) noexcept
    {
        if (state.is_absent)
        {
            out.reset();
            return true;
        }

        Digest parsed;
        if (!digest_from_hex(state.hex, parsed))
        {
            out.reset();
            return false;
        }
        out = parsed;
        return true;
    }
} // namespace ID::Project
