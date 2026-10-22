#pragma once

#include <cmath>
#include <cstddef>

#include "Common/Core.hpp"
#include "Kernel/CoreTypes.hpp"

#include "IDMathScalarVector.hpp"
#include "detail/IDMathScalarDetailFloatBits.hpp"

/**
 *  ID.Math.Scalar 的 `vector` 运算层：只对 `T == float` 且 `N ∈ {2,3,4}` 存在
 *  1. 存储层对所有合法 `T`/`N` 可用，运算层用 `requires` 收窄：`vector<int, 3>`、`vector<float, 8>`
 *     这类组合**存储与访问可用、运算不可用**，调用即编译期失败——没有标量 fallback，
 *     没有隐式提升到 `double`，也没有任何运行期降级或替身路径；
 *  2. 命名边界：`cross` 只对 `vector<float, 3>` 存在；二维叉积用另名 `cross2`，返回标量 z 分量，
 *     **不复用**三维语义；Hadamard（逐分量）积用另名 `hadamard`，不与 `dot`（内积）同名歧义；
 *     刻意不提供 `operator*(vector, vector)`；
 *  3. 失败语义：`try_normalize` 返回 `bool`，任何失败分支都**不改写**输出参数；
 *     `normalize_unchecked` 把"各分量有限且 `squared_length(v) > 0`"写成调用前提，违约即得脏结果；
 *  4. 比较：`equals` 是逐分量精确比较；`near_equal` 用"绝对 + 相对"双容差，判定式写在其文档块与实现里，
 *     不提供默认容差，**没有**全局 epsilon；
 *  5. 精度：不提供 FMA 或 `approx_rsqrt` 一类的近似入口，默认运算保持可预测精度；但**不承诺
 *     bitwise 精度**——`-ffast-math` / `/fp:fast` 允许编译器把 `1/sqrt` 重写成倒数平方根估计
 *     加迭代，`normalize_unchecked` / `try_normalize` 的结果因此可能有 ULP 量级偏差。
 *     精度按 double oracle + 容差判定，不按 bitwise 等价（D0-05）；
 *     `length` / `try_normalize` / `normalize_unchecked` 走 `std::sqrt`，C++20 下 `std::sqrt`
 *     不是 `constexpr`，因此这三个入口不是常量表达式（其余入口尽量 `constexpr`）；这三个入口
 *     含 `sqrt` 与整向量循环，不属于"几次操作"级别的小函数，因此不加 `ID_FORCEINLINE`
 *     强制内联，是否内联交给优化器；
 *  6. 全部入口 `noexcept`、无分配、无 RTTI、无异常、无表达式模板；`vector` 的 `storage` 仍是唯一可写状态。
 */

namespace ID::Math::Scalar
{
    namespace detail
    {
        /**
         *  @brief 单精度绝对值
         *  @param value 输入值；`NaN` 原样返回（任何与它的比较都是 `false`）
         *  @return `value < 0` 时取反，否则原样返回；`-0.0f` 返回自身
         *  @note 不用 `std::fabs`：C++20 下它不是 `constexpr`，而 `near_equal` 要保持可在编译期求值。
         */
        ID_FORCEINLINE constexpr float abs_value(float value) noexcept
        {
            return (value < 0.0f) ? -value : value;
        }
    } // namespace detail

    /**
     *  @brief 逐分量相加
     *  @param lhs 左操作数
     *  @param rhs 右操作数
     *  @return 新向量，第 `i` 个分量为 `lhs[i] + rhs[i]`；不改写任何入参
     *  @note 只对 `T == float` 且 `N ∈ {2,3,4}` 存在；其它组合没有本运算符。
     */
    template <typename T, size_t N>
        requires detail::is_supported_math_shape_v<T, N>
    ID_FORCEINLINE constexpr vector<T, N> operator+(const vector<T, N>& lhs, const vector<T, N>& rhs) noexcept
    {
        vector<T, N> result;
        for (size_t i = 0; i < N; ++i)
        {
            result[i] = lhs[i] + rhs[i];
        }
        return result;
    }

    /**
     *  @brief 逐分量相减
     *  @param lhs 左操作数
     *  @param rhs 右操作数
     *  @return 新向量，第 `i` 个分量为 `lhs[i] - rhs[i]`；不改写任何入参
     *  @note 只对 `T == float` 且 `N ∈ {2,3,4}` 存在；其它组合没有本运算符。
     */
    template <typename T, size_t N>
        requires detail::is_supported_math_shape_v<T, N>
    ID_FORCEINLINE constexpr vector<T, N> operator-(const vector<T, N>& lhs, const vector<T, N>& rhs) noexcept
    {
        vector<T, N> result;
        for (size_t i = 0; i < N; ++i)
        {
            result[i] = lhs[i] - rhs[i];
        }
        return result;
    }

    /**
     *  @brief 向量乘标量：`v * scalar`
     *  @param v 被乘向量
     *  @param scalar 乘数
     *  @return 新向量，第 `i` 个分量为 `v[i] * scalar`
     *  @note 没有 `operator*(vector, vector)`：逐分量积用 `hadamard` 显式命名，避免与内积混淆。
     *        标量按 `T`（即 `float`）取参，`int` / `double` 实参只做一次到 `float` 的标准转换，
     *        运算全程在 `float` 内完成，不产生 `double` 中间结果。
     */
    template <typename T, size_t N>
        requires detail::is_supported_math_shape_v<T, N>
    ID_FORCEINLINE constexpr vector<T, N> operator*(const vector<T, N>& v, T scalar) noexcept
    {
        vector<T, N> result;
        for (size_t i = 0; i < N; ++i)
        {
            result[i] = v[i] * scalar;
        }
        return result;
    }

    /**
     *  @brief 标量乘向量：`scalar * v`（与 `v * scalar` 同义）
     *  @param scalar 乘数
     *  @param v 被乘向量
     *  @return 新向量，第 `i` 个分量为 `scalar * v[i]`
     */
    template <typename T, size_t N>
        requires detail::is_supported_math_shape_v<T, N>
    ID_FORCEINLINE constexpr vector<T, N> operator*(T scalar, const vector<T, N>& v) noexcept
    {
        vector<T, N> result;
        for (size_t i = 0; i < N; ++i)
        {
            result[i] = scalar * v[i];
        }
        return result;
    }

    /**
     *  @brief 向量除以标量：`v / scalar`
     *  @param v 被除数向量
     *  @param scalar 除数；**前提** 有限且不等于 0
     *  @return 新向量，第 `i` 个分量为 `v[i] / scalar`
     *  @note 本入口不做检查、不返回错误码：除数为 0 时得到 `inf` / `NaN` 分量，属调用方违约。
     */
    template <typename T, size_t N>
        requires detail::is_supported_math_shape_v<T, N>
    ID_FORCEINLINE constexpr vector<T, N> operator/(const vector<T, N>& v, T scalar) noexcept
    {
        vector<T, N> result;
        for (size_t i = 0; i < N; ++i)
        {
            result[i] = v[i] / scalar;
        }
        return result;
    }

    /**
     *  @brief Hadamard（逐分量）积
     *  @param lhs 左操作数
     *  @param rhs 右操作数
     *  @return 新向量，第 `i` 个分量为 `lhs[i] * rhs[i]`
     *  @note 与 `dot`（内积，返回标量）是**不同的函数**，不允许同名歧义；也不提供
     *        `operator*(vector, vector)` 来表达它。
     */
    template <typename T, size_t N>
        requires detail::is_supported_math_shape_v<T, N>
    ID_FORCEINLINE constexpr vector<T, N> hadamard(const vector<T, N>& lhs, const vector<T, N>& rhs) noexcept
    {
        vector<T, N> result;
        for (size_t i = 0; i < N; ++i)
        {
            result[i] = lhs[i] * rhs[i];
        }
        return result;
    }

    /**
     *  @brief 内积（点积）
     *  @param lhs 左操作数
     *  @param rhs 右操作数
     *  @return `Σ lhs[i] * rhs[i]`，按 `i` 从 0 到 `N-1` 的顺序累加
     *  @note 累加顺序固定为升序，不做成对求和：`float` 结果与 `double` oracle 之间按容差比较，
     *        不承诺 bitwise 等价（D0-05）。
     */
    template <typename T, size_t N>
        requires detail::is_supported_math_shape_v<T, N>
    ID_FORCEINLINE constexpr T dot(const vector<T, N>& lhs, const vector<T, N>& rhs) noexcept
    {
        T sum = T{0};
        for (size_t i = 0; i < N; ++i)
        {
            sum += lhs[i] * rhs[i];
        }
        return sum;
    }

    /**
     *  @brief 长度平方
     *  @param v 输入向量
     *  @return `dot(v, v)`
     *  @note 先平方再开方会多一次 `sqrt`，需要比较"离原点多远"时优先用本函数。
     */
    template <typename T, size_t N>
        requires detail::is_supported_math_shape_v<T, N>
    ID_FORCEINLINE constexpr T squared_length(const vector<T, N>& v) noexcept
    {
        return dot(v, v);
    }

    /**
     *  @brief 欧几里得长度
     *  @param v 输入向量；**前提** 各分量有限，且 `squared_length(v)` 不溢出到非有限
     *  @return `sqrt(squared_length(v))`；前提被违反时结果可能是 `inf` / `NaN`
     *  @note 走 `std::sqrt`，C++20 下不是常量表达式，因此本入口不给 `constexpr`。
     */
    template <typename T, size_t N>
        requires detail::is_supported_math_shape_v<T, N>
    T length(const vector<T, N>& v) noexcept
    {
        return std::sqrt(squared_length(v));
    }

    /**
     *  @brief 三维叉积
     *  @param lhs 左操作数
     *  @param rhs 右操作数
     *  @return `(lhs.y*rhs.z - lhs.z*rhs.y, lhs.z*rhs.x - lhs.x*rhs.z, lhs.x*rhs.y - lhs.y*rhs.x)`
     *  @note **只对** `vector<float, 3>` 存在（`N == 3` 是硬约束）。二维叉积用另名 `cross2`，
     *        不把二维情形塞进本函数，也不复用本语义。
     */
    template <typename T, size_t N>
        requires detail::is_supported_math_shape_v<T, N> && (N == 3)
    ID_FORCEINLINE constexpr vector<T, N> cross(const vector<T, N>& lhs, const vector<T, N>& rhs) noexcept
    {
        return vector<T, N>(lhs[1] * rhs[2] - lhs[2] * rhs[1],
                            lhs[2] * rhs[0] - lhs[0] * rhs[2],
                            lhs[0] * rhs[1] - lhs[1] * rhs[0]);
    }

    /**
     *  @brief 二维叉积的标量形式（有向面积的两倍，即三维叉积的 z 分量）
     *  @param lhs 左操作数
     *  @param rhs 右操作数
     *  @return `lhs[0] * rhs[1] - lhs[1] * rhs[0]`
     *  @note **只对** `vector<float, 2>` 存在。返回值是标量而不是向量，因此它不是三维
     *        `cross` 的特化，也不复用它的语义：需要二维"垂直向量"时必须由调用方显式构造。
     */
    template <typename T, size_t N>
        requires detail::is_supported_math_shape_v<T, N> && (N == 2)
    ID_FORCEINLINE constexpr T cross2(const vector<T, N>& lhs, const vector<T, N>& rhs) noexcept
    {
        return lhs[0] * rhs[1] - lhs[1] * rhs[0];
    }

    /**
     *  @brief 归一化的 unchecked 入口（热路径）
     *  @param v 源向量；**前提** 各分量有限，且 `squared_length(v) > 0`
     *  @return `v / length(v)`
     *  @note 本入口不做任何检查、不返回错误码：零向量会得到 `inf` / `NaN` 分量（`1/0`），
     *        非有限输入会得到脏结果，两者都属违约；需要"失败时不改输出"时用 `try_normalize`。
     *  @note 不承诺 bitwise 精度：`-ffast-math` / `/fp:fast` 下 `1/sqrt` 可能被改写成倒数平方根
     *        估计加迭代，结果与 double oracle 可差 ULP 量级；判定按容差，不按 bitwise 等价。
     */
    template <typename T, size_t N>
        requires detail::is_supported_math_shape_v<T, N>
    vector<T, N> normalize_unchecked(const vector<T, N>& v) noexcept
    {
        const T inverse_length = T{1} / std::sqrt(squared_length(v));

        vector<T, N> result;
        for (size_t i = 0; i < N; ++i)
        {
            result[i] = v[i] * inverse_length;
        }
        return result;
    }

    /**
     *  @brief 归一化的 checked 入口
     *  @param v 源向量；**前提** 各分量有限（有限输入是 checked 入口的契约前置，D0-05）
     *  @param out 输出；成功时写入 `v / length(v)`，**失败时保持调用前的值不变**
     *  @return 成功 `true`；以下情形 `false`：`v` 含非有限分量、`squared_length(v)` 非有限
     *          （含"分量有限但平方溢出到 `inf`"）、或 `squared_length(v)` 不为正（零向量）
     *  @note 非有限值分类用位模式前检（`detail::is_finite_bits`），不依赖 `isfinite`，
     *        因此不会被 `-ffast-math` / `/fp:fast` 消掉；该前检**只**解决"非有限值分类"，
     *        不构成任何精度或容差结论。
     *        实现先把结果算进临时对象、最后一次性赋值，因此 `&out == &v` 安全，
     *        并且每条失败分支都在写 `out` 之前返回。检查通过后复用 `normalize_unchecked`
     *        完成算术，checked 路径不保留第二份归一化实现。
     */
    template <typename T, size_t N>
        requires detail::is_supported_math_shape_v<T, N>
    bool try_normalize(const vector<T, N>& v, vector<T, N>& out) noexcept
    {
        for (size_t i = 0; i < N; ++i)
        {
            if (!detail::is_finite_bits(v[i]))
            {
                return false;
            }
        }

        const T squared = squared_length(v);
        // 非有限（含有限分量平方后溢出到 inf）与"不为正"都不能归一化：
        // 前者没有可表示的倒数，后者方向未定义。
        if (!detail::is_finite_bits(squared) || !(squared > T{0}))
        {
            return false;
        }

        const vector<T, N> normalized = normalize_unchecked(v);
        out = normalized;
        return true;
    }

    /**
     *  @brief 逐分量精确相等
     *  @param lhs 左操作数
     *  @param rhs 右操作数
     *  @return 全部分量满足 `lhs[i] == rhs[i]` 时为 `true`
     *  @note 这是 `==` 语义的精确比较，不含任何容差：`-0.0f` 与 `0.0f` 相等，`NaN` 与任何值
     *        （含自身）都不相等。需要容差时用 `near_equal`。
     */
    template <typename T, size_t N>
        requires detail::is_supported_math_shape_v<T, N>
    ID_FORCEINLINE constexpr bool equals(const vector<T, N>& lhs, const vector<T, N>& rhs) noexcept
    {
        for (size_t i = 0; i < N; ++i)
        {
            if (!(lhs[i] == rhs[i]))
            {
                return false;
            }
        }
        return true;
    }

    /**
     *  @brief 逐分量"绝对 + 相对"双容差比较
     *  @param lhs 左操作数；**前提** 各分量有限
     *  @param rhs 右操作数；**前提** 各分量有限
     *  @param abs_tolerance 绝对容差；**前提** 有限且 `>= 0`，用于"接近 0"的量级
     *  @param rel_tolerance 相对容差；**前提** 有限且 `>= 0`，按参与比较的量级放大，用于"大数"
     *  @return 全部分量满足下式时为 `true`：
     *          `|lhs[i] - rhs[i]| <= abs_tolerance + rel_tolerance * max(|lhs[i]|, |rhs[i]|)`
     *  @note 判定式逐分量写死在上面的注释与实现里，**没有**内部默认 epsilon：容差必须由调用方按
     *        算法与量级给出（例如几何量常用 `abs_tolerance = 1e-6f`、`rel_tolerance = 1e-5f`，
     *        这只是示例，不是精度承诺）。用 `max(|a|, |b|)` 而不是单侧量级，使判定对称。
     *        `NaN` 参与比较时按 `<=` 的语义返回 `false`（契约外的非有限输入）。
     */
    template <typename T, size_t N>
        requires detail::is_supported_math_shape_v<T, N>
    ID_FORCEINLINE constexpr bool near_equal(const vector<T, N>& lhs, const vector<T, N>& rhs,
                                             T abs_tolerance, T rel_tolerance) noexcept
    {
        for (size_t i = 0; i < N; ++i)
        {
            const T lhs_magnitude = detail::abs_value(lhs[i]);
            const T rhs_magnitude = detail::abs_value(rhs[i]);
            const T difference    = detail::abs_value(lhs[i] - rhs[i]);
            const T magnitude     = (lhs_magnitude > rhs_magnitude) ? lhs_magnitude : rhs_magnitude;

            if (!(difference <= abs_tolerance + rel_tolerance * magnitude))
            {
                return false;
            }
        }
        return true;
    }

    // 运算集合的编译期边界自证：存储可用的组合不等于运算可用。
    // 这几条断言把"哪些组合必须失败"钉在头文件里，不依赖调用方另写负向用例才发现回归。
    namespace detail
    {
        /// `lhs + rhs` 是否可编译
        template <typename Lhs, typename Rhs>
        concept is_addable_with = requires(const Lhs& lhs, const Rhs& rhs) { lhs + rhs; };

        /// `dot(lhs, rhs)` 是否可编译（含 ADL）
        template <typename Lhs, typename Rhs>
        concept is_dottable_with = requires(const Lhs& lhs, const Rhs& rhs) { dot(lhs, rhs); };

        /// `cross(lhs, rhs)` 是否可编译（含 ADL）
        template <typename Lhs, typename Rhs>
        concept is_crossable_with = requires(const Lhs& lhs, const Rhs& rhs) { cross(lhs, rhs); };

        /// `length(lhs)` 是否可编译（含 ADL）
        template <typename Value>
        concept is_length_taking = requires(const Value& v) { length(v); };
    } // namespace detail

    static_assert(detail::is_addable_with<vec2, vec2> && detail::is_addable_with<vec3, vec3>
                      && detail::is_addable_with<vec4, vec4>,
                  "vec2 / vec3 / vec4 必须支持逐分量加法");
    static_assert(!detail::is_addable_with<vector<int, 3>, vector<int, 3>>,
                  "vector<int,3> 的存储可用，但加法必须不存在（不做标量 fallback、不提升到 double）");
    static_assert(!detail::is_addable_with<vector<float, 8>, vector<float, 8>>,
                  "vector<float,8> 的存储可用，但加法必须不存在（运算只对 N ∈ {2,3,4}）");

    static_assert(detail::is_dottable_with<vec2, vec2> && detail::is_dottable_with<vec3, vec3>
                      && detail::is_dottable_with<vec4, vec4>,
                  "vec2 / vec3 / vec4 必须支持内积");
    static_assert(!detail::is_dottable_with<vector<int, 3>, vector<int, 3>>,
                  "vector<int,3> 必须没有 dot：运算层只对 float 存在");
    static_assert(!detail::is_dottable_with<vector<float, 8>, vector<float, 8>>,
                  "vector<float,8> 必须没有 dot：运算层只对 N ∈ {2,3,4} 存在");

    static_assert(detail::is_length_taking<vec2> && detail::is_length_taking<vec3>
                      && detail::is_length_taking<vec4>,
                  "length / squared_length 对 N = 2 / 3 / 4 都存在（vec2 也合法）");
    static_assert(!detail::is_length_taking<vector<float, 8>>,
                  "vector<float,8> 必须没有 length");

    static_assert(detail::is_crossable_with<vec3, vec3>, "cross 只对 vec3 存在");
    static_assert(!detail::is_crossable_with<vec2, vec2> && !detail::is_crossable_with<vec4, vec4>,
                  "cross 只对 vec3 存在：二维用 cross2（另名），四维没有叉积");
} // namespace ID::Math::Scalar
