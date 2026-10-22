// 反向编译用例（backend = ID.Math.Scalar）：右值矩阵的 `operator[]` 必须**编译失败**。
//
// 拒绝点：`matrix` 把 `operator[](size_t) &&` 与 `const&&` 显式 `= delete`（D0-04：代理保存引用，
// 临时矩阵在完整表达式后销毁，代理必然悬空）。本 TU 声明"右值下标必须可用"，正确实现下静态断言
// 失败并带 EXPECTED 类别；若实现回归成"允许右值下标"，本 TU 会编译通过 → 驱动器判失败。

#include <utility>

#include "IDMathScalarApi.hpp"

namespace
{
    namespace scalar = ID::Math::Scalar;

    template <typename MatrixType>
    concept rvalue_subscriptable = requires(MatrixType&& value) { std::move(value)[0u]; };
} // namespace

static_assert(rvalue_subscriptable<scalar::matrix<3, 3>>, "EXPECTED:math.scalar.rvalue_subscript_deleted");
