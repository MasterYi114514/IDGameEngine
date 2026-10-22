// 反向编译用例（backend = ID.Math.Scalar）：`vector<float,8>` 的**运算必须编译失败**。
//
// 拒绝点：运算层只对 N ∈ {2,3,4} 存在（`is_supported_math_shape_v`），存储层对任意合法 N 都可用。
// 本 TU 声明"加法必须存在"，因此正确实现下静态断言必然失败并带 EXPECTED 类别；
// 若实现回归成"对任意 N 都提供运算"，本 TU 会编译通过 → 驱动器判失败。

#include "IDMathScalarApi.hpp"

namespace
{
    namespace scalar = ID::Math::Scalar;

    template <typename Lhs, typename Rhs>
    concept addable = requires(const Lhs& lhs, const Rhs& rhs) { lhs + rhs; };
} // namespace

static_assert(addable<scalar::vector<float, 8>, scalar::vector<float, 8>>,
              "EXPECTED:math.scalar.vector_float8_add_absent");
