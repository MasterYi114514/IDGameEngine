// 反向编译用例（backend = ID.Math.Scalar）：内维不匹配的矩阵乘必须**编译失败**。
//
// 拒绝点：`operator*(matrix<Rows,K>, matrix<K,Cols>)` 的内维由模板参数强制相等；`matrix<2,3> *
// matrix<2,4>` 没有可用的 `operator*`（不做"按维度猜"的乘法，也没有逐元素乘）。本 TU 声明该乘法
// 必须存在，正确实现下静态断言失败并带 EXPECTED 类别；若实现回归成"按维度猜乘法"，本 TU 会编译
// 通过 → 驱动器判失败。

#include "IDMathScalarApi.hpp"

namespace
{
    namespace scalar = ID::Math::Scalar;

    template <typename Lhs, typename Rhs>
    concept multipliable = requires(const Lhs& lhs, const Rhs& rhs) { lhs * rhs; };
} // namespace

static_assert(multipliable<scalar::matrix<2, 3>, scalar::matrix<2, 4>>,
              "EXPECTED:math.scalar.matmul_dimension_mismatch_absent");
