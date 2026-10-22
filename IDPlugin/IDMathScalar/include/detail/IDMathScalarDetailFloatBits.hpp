#pragma once

#include <bit>
#include <limits>

#include "Common/Core.hpp"
#include "Kernel/CoreTypes.hpp"

/**
 *  ID.Math.Scalar 的私有浮点分类工具（detail 目录不属于对外承诺）
 *  1. 只解决"非有限值分类"一件事：按 IEEE-754 binary32 的位模式判定 `inf` / `NaN`，
 *     不使用 `isfinite` 家族，避免被 `-ffast-math` / `/fp:fast` 的"无 inf/NaN"假定消掉；
 *  2. 位模式前检**不构成**任何精度、容差或比较结论（D0-05）：它只回答"是不是非有限值"；
 *  3. 不修改进程浮点全局状态（不碰 FTZ/DAZ、不改舍入模式），也不引入独立 strict TU；
 *  4. 只接受 `float`：`double` / `long double` 不在本 backend 的运算集合内，不在此顺手扩大范围。
 */

namespace ID::Math::Scalar::detail
{
    // 位模式判定要求单精度就是 IEEE-754 binary32：否则"指数位全 1"这种判据根本不成立。
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
                  "位模式前检要求 float 是 IEEE-754 binary32；本 backend 的运算集合只对 float 存在");

    /// IEEE-754 binary32 的指数位；全 1 表示 `inf` 或 `NaN`
    inline constexpr uint32_t kFloatExponentBits = 0x7F800000u;

    /**
     *  @brief 有限值判定（按位模式，不按浮点比较）
     *  @param value 待判定值
     *  @return 指数位不全为 1 时为 `true`，即既不是 `inf` 也不是 `NaN`
     *  @note 不依赖 `isfinite`，因此不会被 `-ffast-math` / `/fp:fast` 的"无 inf/NaN"假定消除；
     *        对 `±0` 与 `subnormal` 一律返回 `true`。本函数只做分类，不给出任何精度结论。
     */
    ID_FORCEINLINE constexpr bool is_finite_bits(float value) noexcept
    {
        return (std::bit_cast<uint32_t>(value) & kFloatExponentBits) != kFloatExponentBits;
    }
} // namespace ID::Math::Scalar::detail
