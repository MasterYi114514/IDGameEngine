// 反向编译用例（backend = ID.Math.SIMD）：`vector<float,8>` 的**运算必须编译失败**。
//
// 拒绝点：运算层只对 N ∈ {2,3,4} 存在（`is_math_operand_v`），存储层对任意合法 N 都可用
//（`vector<float,8>` 有 8 个存储槽位、sizeof 32 字节，但没有任何运算符）。
// 本 TU 声明"加法必须存在"，因此正确实现下静态断言必然失败并带 EXPECTED 类别；
// 若实现回归成"对任意 N 都提供运算"，本 TU 会编译通过 → 驱动器判失败。

#include "IDMathSIMDApi.hpp"

namespace
{
    namespace simd = ID::Math::SIMD;

    template <typename Lhs, typename Rhs>
    concept addable = requires(const Lhs& lhs, const Rhs& rhs) { lhs + rhs; };
} // namespace

static_assert(addable<simd::vector<float, 8>, simd::vector<float, 8>>,
              "EXPECTED:math.simd.vector_float8_add_absent");
