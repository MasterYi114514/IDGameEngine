// 反向编译用例（backend = ID.Math.SIMD）：内维不匹配的矩阵乘必须**编译失败**。
//
// 拒绝点：`operator*(matrix<R,K>, matrix<K,C>)` 的内维由模板参数强制相等；`matrix<2,3> *
// matrix<2,4>` 模板推导失败（不做"按维度猜"的乘法，也没有逐元素乘）。本 TU 声明该乘法必须存在，
// 正确实现下静态断言失败并带 EXPECTED 类别；若实现回归成"按维度猜乘法"，本 TU 会编译通过 →
// 驱动器判失败。

#include "IDMathSIMDApi.hpp"

namespace
{
    namespace simd = ID::Math::SIMD;

    template <typename Lhs, typename Rhs>
    concept multipliable = requires(const Lhs& lhs, const Rhs& rhs) { lhs * rhs; };
} // namespace

static_assert(multipliable<simd::matrix<2, 3>, simd::matrix<2, 4>>,
              "EXPECTED:math.simd.matmul_dimension_mismatch_absent");
