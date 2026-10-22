// 反向编译用例（backend = ID.Math.SIMD）：`cross(vec2, vec2)` 必须**编译失败**。
//
// 拒绝点：`cross` 的 requires 里带 `(N == 3)` 硬约束；二维叉积是另名 `cross2`（返回标量），
// 不复用三维语义。本 TU 声明"cross 必须接受 vec2"，正确实现下静态断言失败并带 EXPECTED 类别；
// 若实现回归成"把二维情形塞进 cross"，本 TU 会编译通过 → 驱动器判失败。

#include "IDMathSIMDApi.hpp"

namespace
{
    namespace simd = ID::Math::SIMD;

    template <typename Lhs, typename Rhs>
    concept crossable = requires(const Lhs& lhs, const Rhs& rhs) { simd::cross(lhs, rhs); };
} // namespace

static_assert(crossable<simd::vec2, simd::vec2>, "EXPECTED:math.simd.cross_vec2_absent");
