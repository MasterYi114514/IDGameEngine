// 反向编译用例（backend = ID.Math.Scalar）：`vector<int,3>` 的**运算必须编译失败**。
//
// 拒绝点：公开头运算层用 `requires detail::is_supported_math_shape_v<T, N>` 收窄（只对 float 且
// N ∈ {2,3,4} 存在运算符），存储层仍可用。本 TU 反过来声明"加法必须存在"：
//   - 正确实现下概念为 false → 下面的静态断言失败 → 编译失败，诊断里恰好 1 条静态断言 + EXPECTED 类别；
//   - 若实现回归成"给 int 做标量 fallback"，本 TU 会编译通过 → 驱动器判失败。
// 同环境的合法对照见 legal_control_scalar.cpp（必须先证明它能编过）。

#include "IDMathScalarApi.hpp"

namespace
{
    namespace scalar = ID::Math::Scalar;

    template <typename Lhs, typename Rhs>
    concept addable = requires(const Lhs& lhs, const Rhs& rhs) { lhs + rhs; };
} // namespace

static_assert(addable<scalar::vector<int, 3>, scalar::vector<int, 3>>,
              "EXPECTED:math.scalar.vector_int3_add_absent");
