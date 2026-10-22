#pragma once

#include <cmath>
#include <cstddef>

#include "Common/Core.hpp"
#include "Kernel/CoreTypes.hpp"

#include "IDMathScalarMatrix.hpp"
#include "IDMathScalarVector.hpp"
#include "detail/IDMathScalarDetailFloatBits.hpp"

/**
 *  ID.Math.Scalar 的 `matrix` 运算层：只对 `Rows, Cols ∈ {2,3,4}` 存在
 *  1. v1 运算集合：`matrix<R,K> * matrix<K,C> → matrix<R,C>`、
 *     `matrix<R,C> * vector<float,C> → vector<float,R>`、`transpose(matrix<R,C>) → matrix<C,R>`、
 *     `identity<N>()`、`determinant(m)`、`try_inverse(m, out)`；
 *     **没有**逐元素 `matrix * matrix`（Hadamard 积不在矩阵层提供），也没有"按维度猜"的乘法：
 *     内维不相等就没有 `operator*`，只有列向量形态的 `matrix * vector`，没有 `vector * matrix`；
 *  2. 方阵专用（`N ∈ {2,3,4}`）：`identity` / `determinant` / `try_inverse`。非方阵与
 *     `N ∉ {2,3,4}` 的形状**存储可用、运算不可用**，调用即编译期失败——没有标量 fallback，
 *     没有隐式提升到 `double`，也没有任何运行期降级或替身路径；
 *  3. 逆矩阵**只有** checked 入口 `try_inverse`：近奇异判定与 scale 相关，失败时输出参数
 *     **不变**；刻意不提供"非 checked 的 `inverse`"——那正是"静默给出脏结果"的入口（D0-05）；
 *  4. checked 入口以 finite input 为契约前置：非有限值分类用位模式前检
 *     （`detail::is_finite_bits`），不依赖 `isfinite`，因此不会被 `-ffast-math` / `/fp:fast` 消掉；
 *     该前检**只**解决"非有限值分类"，不构成任何精度或容差结论（D0-05）；
 *  5. 精度：`determinant` / `try_inverse` 全部在 `float` 内完成，不产生 `double` 中间结果，
 *     也不承诺 bitwise 精度；判定按 double oracle + 容差，不按 bitwise 等价（D0-05）。
 *     `try_inverse` 需要列长度（`std::sqrt`），C++20 下 `std::sqrt` 不是常量表达式，
 *     因此本入口不是 `constexpr`；其余入口尽量保持可在编译期求值；
 *  6. 全部入口 `noexcept`、无分配、无 RTTI、无异常、无表达式模板；矩阵的 `storage` 仍是唯一可写状态；
 *  7. `determinant` / `transpose` / 乘法都是 raw 算术入口，不做输入前检：非有限输入属契约外，
 *     需要失败语义时用 `try_inverse` 这类 checked 入口。
 */

namespace ID::Math::Scalar
{
    namespace detail
    {
        /**
         *  @brief 运算层允许的矩阵形状：`Rows, Cols ∈ {2,3,4}`
         *  @tparam Rows 逻辑行数
         *  @tparam Cols 逻辑列数
         *  @note 存储层对任意正维度可用，运算层只对这个组合存在：`matrix<5,5>`、`matrix<2,8>`
         *        之类的实例化**没有**运算符与自由函数，调用即编译期失败。
         */
        template <size_t Rows, size_t Cols>
        inline constexpr bool is_supported_matrix_shape_v =
            ((Rows == 2) || (Rows == 3) || (Rows == 4)) && ((Cols == 2) || (Cols == 3) || (Cols == 4));

        /**
         *  @brief 方阵专用运算允许的维度：`N ∈ {2,3,4}`
         *  @tparam N 方阵阶数
         *  @note `identity` / `determinant` / `try_inverse` 只对本组合存在。
         */
        template <size_t N>
        inline constexpr bool is_supported_square_dimension_v = (N == 2) || (N == 3) || (N == 4);

        /**
         *  @brief 余子式行列式：去掉第 `row_skip` 行与第 `col_skip` 列后的 `(N-1) × (N-1)` 行列式
         *  @tparam N 母矩阵阶数（2 / 3 / 4）
         *  @param m 母矩阵
         *  @param row_skip 要去掉的行下标；**前提** `row_skip < N`
         *  @param col_skip 要去掉的列下标；**前提** `col_skip < N`
         *  @return `(N-1)` 阶行列式；1 阶余子式就是剩下的那个元素
         *  @note 三档都是展开式，没有循环、没有临时矩阵：`N == 2` 时余子式是 1 阶（一个元素），
         *        `N == 3` 时是 2 阶（两项），`N == 4` 时是 3 阶（六项）。
         *        "跳过某下标"用的是三条取值：`skip == 0` 取 `1`，比 skip 小的取自身，
         *        其余取 `+1`，写成下面的三元链以免依赖循环与动态下标。
         */
        template <size_t N>
        ID_FORCEINLINE constexpr float minor_determinant(const matrix<N, N>& m, size_t row_skip,
                                                        size_t col_skip) noexcept
        {
            static_assert(is_supported_square_dimension_v<N>,
                          "余子式只对 2 / 3 / 4 阶方阵存在：matrix 的运算集合不覆盖其它维度");

            if constexpr (N == 2)
            {
                // 1 阶余子式：去掉一行一列后只剩一个元素
                return m.get_element(row_skip == 0 ? 1 : 0, col_skip == 0 ? 1 : 0);
            }
            else if constexpr (N == 3)
            {
                // 2 阶余子式：ad - bc
                const size_t r0 = (row_skip == 0) ? 1 : 0;
                const size_t r1 = (row_skip == 2) ? 1 : 2;
                const size_t c0 = (col_skip == 0) ? 1 : 0;
                const size_t c1 = (col_skip == 2) ? 1 : 2;

                return m.get_element(r0, c0) * m.get_element(r1, c1)
                       - m.get_element(r0, c1) * m.get_element(r1, c0);
            }
            else
            {
                static_assert(N == 4, "余子式只对 2 / 3 / 4 阶方阵存在：matrix 的运算集合不覆盖其它维度");

                // 3 阶余子式：沿其首行展开成三个 2 阶余子式
                const size_t r0 = (row_skip == 0) ? 1 : 0;
                const size_t r1 = (row_skip <= 1) ? 2 : 1;
                const size_t r2 = (row_skip <= 2) ? 3 : 2;
                const size_t c0 = (col_skip == 0) ? 1 : 0;
                const size_t c1 = (col_skip <= 1) ? 2 : 1;
                const size_t c2 = (col_skip <= 2) ? 3 : 2;

                return m.get_element(r0, c0) * (m.get_element(r1, c1) * m.get_element(r2, c2)
                                                - m.get_element(r1, c2) * m.get_element(r2, c1))
                       - m.get_element(r0, c1) * (m.get_element(r1, c0) * m.get_element(r2, c2)
                                                  - m.get_element(r1, c2) * m.get_element(r2, c0))
                       + m.get_element(r0, c2) * (m.get_element(r1, c0) * m.get_element(r2, c1)
                                                  - m.get_element(r1, c1) * m.get_element(r2, c0));
            }
        }
    } // namespace detail

    /**
     *  @brief 矩阵乘：`matrix<Rows, K> * matrix<K, Cols>` -> `matrix<Rows, Cols>`
     *  @tparam Rows 左操作数的行数
     *  @tparam K 公共内维（左操作数的列数 = 右操作数的行数）
     *  @tparam Cols 右操作数的列数
     *  @param lhs 左操作数
     *  @param rhs 右操作数
     *  @return 乘积矩阵，第 `(r, c)` 个元素为 `Σ_k lhs(r,k) × rhs(k,c)`，累加顺序固定为 `k` 升序
     *  @note 内维由模板参数强制相等：`matrix<2,3> * matrix<2,4>` 没有这个运算符，不做"按维度猜"
     *        的乘法，也不做逐元素乘（那需要显式的 Hadamard 命名，本 backend 在矩阵层不提供）。
     *        `float` 累加，不产生 `double` 中间结果；结果与 double oracle 按容差比较，
     *        不承诺 bitwise 等价（D0-05）。
     */
    template <size_t Rows, size_t K, size_t Cols>
        requires detail::is_supported_matrix_shape_v<Rows, K> && detail::is_supported_matrix_shape_v<K, Cols>
    ID_FORCEINLINE constexpr matrix<Rows, Cols> operator*(const matrix<Rows, K>& lhs,
                                                          const matrix<K, Cols>& rhs) noexcept
    {
        matrix<Rows, Cols> result;
        for (size_t c = 0; c < Cols; ++c)
        {
            for (size_t r = 0; r < Rows; ++r)
            {
                float sum = 0.0f;
                for (size_t k = 0; k < K; ++k)
                {
                    sum += lhs.get_element(r, k) * rhs.get_element(k, c);
                }
                result.get_element(r, c) = sum;
            }
        }
        return result;
    }

    /**
     *  @brief 矩阵乘列向量：`matrix<Rows, Cols> * vector<float, Cols>` -> `vector<float, Rows>`
     *  @tparam Rows 矩阵行数（= 结果向量的分量数）
     *  @tparam Cols 矩阵列数（= 向量分量数）
     *  @param m 左操作数
     *  @param v 右操作数（列向量）
     *  @return 结果向量，第 `r` 个分量为 `Σ_c m(r,c) × v[c]`
     *  @note 只有"矩阵 × 列向量"这一个方向：`vector * matrix` 不存在（那是行向量约定，
     *        需要时由调用方显式 `transpose`）。
     */
    template <size_t Rows, size_t Cols>
        requires detail::is_supported_matrix_shape_v<Rows, Cols>
    ID_FORCEINLINE constexpr vector<float, Rows> operator*(const matrix<Rows, Cols>& m,
                                                           const vector<float, Cols>& v) noexcept
    {
        vector<float, Rows> result;
        for (size_t r = 0; r < Rows; ++r)
        {
            float sum = 0.0f;
            for (size_t c = 0; c < Cols; ++c)
            {
                sum += m.get_element(r, c) * v[c];
            }
            result[r] = sum;
        }
        return result;
    }

    /**
     *  @brief 转置：`transpose(matrix<Rows, Cols>)` -> `matrix<Cols, Rows>`
     *  @tparam Rows 源矩阵行数（= 结果矩阵列数）
     *  @tparam Cols 源矩阵列数（= 结果矩阵行数）
     *  @param m 源矩阵
     *  @return 转置矩阵，第 `(r, c)` 个元素为 `m(c, r)`
     *  @note 矩形与方阵走同一实现；转置只重排元素，不改变存储约定（结果仍是列主序紧凑矩阵）。
     */
    template <size_t Rows, size_t Cols>
        requires detail::is_supported_matrix_shape_v<Rows, Cols>
    ID_FORCEINLINE constexpr matrix<Cols, Rows> transpose(const matrix<Rows, Cols>& m) noexcept
    {
        matrix<Cols, Rows> result;
        for (size_t c = 0; c < Cols; ++c)
        {
            for (size_t r = 0; r < Rows; ++r)
            {
                result.get_element(c, r) = m.get_element(r, c);
            }
        }
        return result;
    }

    /**
     *  @brief 单位矩阵（必须显式调用，不得用默认构造表示）
     *  @tparam N 阶数：2 / 3 / 4
     *  @return 对角线为 `1`、其余元素为 `0` 的 `matrix<N, N>`
     *  @note 默认构造得到零矩阵，与单位矩阵是两个不同的值；因此 `identity` 是显式入口（D0-03）。
     */
    template <size_t N>
        requires detail::is_supported_square_dimension_v<N>
    ID_FORCEINLINE constexpr matrix<N, N> identity() noexcept
    {
        matrix<N, N> result;
        for (size_t i = 0; i < N; ++i)
        {
            result.get_element(i, i) = 1.0f;
        }
        return result;
    }

    /**
     *  @brief 行列式（只对方阵 2 / 3 / 4 存在）
     *  @tparam N 阶数：2 / 3 / 4
     *  @param m 输入矩阵；**前提** 各元素有限
     *  @return 行列式；沿第 0 行做 Laplace 展开，余子式阶数由 `N` 决定
     *  @note 非方阵没有本函数，`N ∉ {2,3,4}` 也没有——不存在按维度猜的实现分支。
     *        本入口是 raw 算术入口：非有限输入、量级溢出都会得到脏结果，属调用方违约；
     *        需要失败语义时用 `try_inverse`。`float` 内完成，不提升到 `double`。
     */
    template <size_t N>
        requires detail::is_supported_square_dimension_v<N>
    ID_FORCEINLINE constexpr float determinant(const matrix<N, N>& m) noexcept
    {
        float det = 0.0f;
        for (size_t c = 0; c < N; ++c)
        {
            const float term = m.get_element(0, c) * detail::minor_determinant(m, 0, c);
            det += ((c & 1u) == 0u) ? term : -term;
        }
        return det;
    }

    /**
     *  @brief 逆矩阵的近奇异阈值（无量纲）
     *  @note 判定式见 `try_inverse`：列归一化后的 `|det|` 与本阈值比较。它是**判定阈值**，
     *        不是精度承诺，也不是条件数估计；不得据此宣称逆矩阵的精度。
     */
    inline constexpr float kInverseNearSingularThreshold = 1e-6f;

    /**
     *  @brief 逆矩阵的 checked 入口（唯一的求逆入口）
     *  @tparam N 阶数：2 / 3 / 4
     *  @param m 输入矩阵；**前提** 各元素有限（有限输入是 checked 入口的契约前置，D0-05）
     *  @param out 输出；成功时写入 `m` 的逆，**失败时保持调用前的值不变**
     *  @return 成功 `true`；以下情形 `false`：`m` 含非有限元素、`m` 的某一列为零向量、
     *          行列式非有限、`m` 近奇异（见下）、逆矩阵任一元素非有限
     *  @note 近奇异判定与 **scale 相关**：先把每一列按其 2-范数归一化得到 `Ĉ`，再比较
     *        `|det(Ĉ)| <= kInverseNearSingularThreshold`。因为
     *        `|det(Ĉ)| == |det(m)| / Π_j ||col_j(m)||₂`，该判据对任意非零列缩放保持不变，
     *        因此"整体乘 1e-3"或"某一列乘 1e6"都不会改变判定结果（这与"用绝对行列式阈值"
     *        的做法不同，后者会把量级当成奇异性）。
     *        判据只回答"是否近奇异"，**不是**条件数估计，也不构成精度或容差结论（D0-05）。
     *        实现先把候选结果算进临时矩阵、逐元素检查有限性，全部通过后才一次性赋给 `out`，
     *        因此任何失败分支都不改写 `out`，`&out == &m` 也安全。
     *        极端量级（元素量级使行列式或伴随矩阵溢出/下溢）下会**保守失败**：宁可返回
     *        `false`，也不会给出"静默的脏结果"。需要列长度，故本入口不是 `constexpr`。
     *        结果里可能出现 `-0.0f`（代数余子式取负的自然结果）：它与 `+0.0f` 数值相等，
     *        本接口**不承诺符号零**，按位比较两个"应该相等"的逆矩阵不是契约。
     */
    template <size_t N>
        requires detail::is_supported_square_dimension_v<N>
    bool try_inverse(const matrix<N, N>& m, matrix<N, N>& out) noexcept
    {
        // 1. 有限输入前检：位模式判定，不依赖 isfinite（D0-05）
        for (size_t c = 0; c < N; ++c)
        {
            for (size_t r = 0; r < N; ++r)
            {
                if (!detail::is_finite_bits(m.get_element(r, c)))
                {
                    return false;
                }
            }
        }

        // 2. 行列式：非有限即没有可用逆
        const float det = determinant(m);
        if (!detail::is_finite_bits(det))
        {
            return false;
        }

        // 3. 尺度相关的近奇异判定：列归一化后重算行列式（= |det| / Π 列长度）
        matrix<N, N> normalized_columns;
        for (size_t c = 0; c < N; ++c)
        {
            float squared_length = 0.0f;
            for (size_t r = 0; r < N; ++r)
            {
                const float element = m.get_element(r, c);
                squared_length += element * element;
            }

            // 零列或平方溢出都意味着这一列不可归一化：列必然线性相关
            if (!detail::is_finite_bits(squared_length) || !(squared_length > 0.0f))
            {
                return false;
            }

            const float inverse_column_length = 1.0f / std::sqrt(squared_length);
            if (!detail::is_finite_bits(inverse_column_length))
            {
                return false;
            }

            for (size_t r = 0; r < N; ++r)
            {
                normalized_columns.get_element(r, c) = m.get_element(r, c) * inverse_column_length;
            }
        }

        const float normalized_det = determinant(normalized_columns);
        // 取绝对值用条件取反而不是 std::fabs：判定只需要一次比较，也少一层库函数语义依赖
        const float volume         = (normalized_det < 0.0f) ? -normalized_det : normalized_det;
        if (!detail::is_finite_bits(volume) || !(volume > kInverseNearSingularThreshold))
        {
            return false;
        }

        // 4. 伴随矩阵 / det：A⁻¹(r,c) = C(c,r)/det = (-1)^(c+r) · M(c,r) / det
        const float inverse_det = 1.0f / det;
        if (!detail::is_finite_bits(inverse_det))
        {
            return false;
        }

        matrix<N, N> candidate;
        for (size_t c = 0; c < N; ++c)
        {
            for (size_t r = 0; r < N; ++r)
            {
                const float minor = detail::minor_determinant(m, c, r);
                const float value = ((((r + c) & 1u) == 0u) ? minor : -minor) * inverse_det;
                if (!detail::is_finite_bits(value))
                {
                    return false;
                }
                candidate.get_element(r, c) = value;
            }
        }

        out = candidate;
        return true;
    }

    // 运算集合的编译期边界自证：存储可用的形状不等于运算可用。
    // 这几条断言把"哪些组合必须失败"钉在头文件里，不依赖调用方另写负向用例才发现回归。
    namespace detail
    {
        /// `lhs * rhs` 是否可编译（矩阵乘 / 矩阵-列向量乘，含 ADL）
        template <typename Lhs, typename Rhs>
        concept is_multiplicable_with = requires(const Lhs& lhs, const Rhs& rhs) { lhs * rhs; };

        /// `transpose(m)` 是否可编译（含 ADL）
        template <typename Matrix>
        concept is_transposable = requires(const Matrix& m) { transpose(m); };

        /// `determinant(m)` 是否可编译（含 ADL）
        template <typename Matrix>
        concept is_determinant_taking = requires(const Matrix& m) { determinant(m); };

        /// `try_inverse(m, out)` 是否可编译（含 ADL）
        template <typename Matrix>
        concept is_inversible = requires(const Matrix& m, Matrix& out) { try_inverse(m, out); };

        /// `identity<N>()` 是否可编译（含 ADL）
        template <size_t N>
        concept is_identity_dimension = requires { identity<N>(); };
    } // namespace detail

    static_assert(detail::is_multiplicable_with<matrix<2, 3>, matrix<3, 4>>
                      && detail::is_multiplicable_with<matrix<4, 4>, matrix<4, 2>>
                      && detail::is_multiplicable_with<matrix<3, 3>, matrix<3, 3>>,
                  "matrix<R,K> * matrix<K,C> 对 R/K/C ∈ {2,3,4} 必须存在（含矩形）");
    static_assert(!detail::is_multiplicable_with<matrix<2, 3>, matrix<2, 4>>,
                  "内维不等的矩阵乘必须不存在：不得按维度猜乘法");
    static_assert(!detail::is_multiplicable_with<matrix<5, 5>, matrix<5, 5>>,
                  "matrix<5,5> 的存储可用，但乘法必须不存在（运算只对 2/3/4）");

    static_assert(detail::is_multiplicable_with<matrix<2, 3>, vector<float, 3>>,
                  "matrix<R,C> * vector<float,C> 必须存在");
    static_assert(!detail::is_multiplicable_with<matrix<2, 3>, vector<float, 2>>,
                  "内维不等的矩阵-向量乘必须不存在");
    static_assert(!detail::is_multiplicable_with<matrix<2, 3>, vector<float, 8>>,
                  "vector<float,8> 参与矩阵乘必须编译失败（运算只对 N ∈ {2,3,4} 存在）");
    static_assert(!detail::is_multiplicable_with<vector<float, 3>, matrix<3, 2>>,
                  "列向量约定：只提供 matrix * vector，不提供 vector * matrix");

    static_assert(detail::is_transposable<matrix<2, 3>> && detail::is_transposable<matrix<3, 3>>
                      && detail::is_transposable<matrix<4, 2>>,
                  "transpose 对 v1 的任意形状（含矩形）都必须存在");
    static_assert(!detail::is_transposable<matrix<5, 2>>,
                  "matrix<5,2> 的存储可用，但 transpose 必须不存在（运算只对 2/3/4）");

    static_assert(detail::is_determinant_taking<matrix<2, 2>> && detail::is_determinant_taking<matrix<3, 3>>
                      && detail::is_determinant_taking<matrix<4, 4>>,
                  "determinant 只对方阵 2 / 3 / 4 存在");
    static_assert(!detail::is_determinant_taking<matrix<2, 3>> && !detail::is_determinant_taking<matrix<4, 2>>,
                  "determinant 不接受非方阵：没有按维度猜的实现分支");
    static_assert(!detail::is_determinant_taking<matrix<5, 5>>,
                  "determinant 只对 2 / 3 / 4 阶方阵存在");

    static_assert(detail::is_inversible<matrix<2, 2>> && detail::is_inversible<matrix<3, 3>>
                      && detail::is_inversible<matrix<4, 4>>,
                  "try_inverse 只对方阵 2 / 3 / 4 存在");
    static_assert(!detail::is_inversible<matrix<3, 4>> && !detail::is_inversible<matrix<4, 2>>,
                  "try_inverse 不接受非方阵");

    static_assert(detail::is_identity_dimension<2> && detail::is_identity_dimension<3>
                      && detail::is_identity_dimension<4>,
                  "identity<N> 对 N ∈ {2,3,4} 存在");
    static_assert(!detail::is_identity_dimension<5> && !detail::is_identity_dimension<1>,
                  "identity 只对 2 / 3 / 4 阶方阵存在");
} // namespace ID::Math::Scalar
