#pragma once

#include <cmath>
#include <cstddef>

#include "Common/Core.hpp"
#include "Kernel/CoreTypes.hpp"

#include "IDMathSIMDVector.hpp"
#include "detail/IDMathSIMDDetailFloatBits.hpp"
#include "detail/IDMathSIMDDetailSse2.hpp"

/**
 *  ID.Math.SIMD 的 `vector` 运算层：只对 `T == float` 且 `N ∈ {2,3,4}` 存在
 *  1. 存储层对所有合法 `T`/`N` 可用，运算层用 `requires detail::is_math_operand_v<T, N>` 收窄：
 *     `vector<int, 3>`、`vector<float, 8>` 这类组合**存储与访问可用、运算不可用**，调用即编译期
 *     失败——没有标量 fallback，没有隐式提升到 `double`，也没有任何运行期降级或替身路径；
 *  2. **实现方式**：每个逐分量运算都是"从 `storage` 载入 128-bit → 在寄存器里算 → 写回 `storage`"，
 *     中间没有任何持久化的 intrinsic 值（对象里仍然只有 `vector::storage` 一份可写状态）；
 *     载入/写回的掩码、对齐前提与 `vec2` 的 8 字节约束集中在
 *     `detail/IDMathSIMDDetailSse2.hpp`，本头不出现 `reinterpret_cast`，也没有 `union` punning；
 *  3. **padding 纪律**：`vec3` 的 `lane3` 是 padding——载入时按位清零，因此 padding 里的 `NaN`/`Inf`
 *     不进入任何算术、不参与 `dot`/`length`/`try_normalize` 的聚合；写回时再清零一次，使结果对象的
 *     padding lane 固定为 `+0.0f`。`vec2` 只做 8 字节读写，绝不触碰它的 ABI 尾部填充；
 *  4. 命名边界：`cross` 只对 `vector<float, 3>` 存在；二维叉积用另名 `cross2`，返回标量，
 *     **不复用**三维语义；Hadamard（逐分量）积用另名 `hadamard`，不与 `dot`（内积）同名歧义；
 *     刻意不提供 `operator*(vector, vector)`；
 *  5. 失败语义：`try_normalize` 返回 `bool`，任何失败分支都**不改写**输出参数，实现先把结果算进
 *     临时对象再一次性赋值（因此 `&out == &v` 安全）；`normalize_unchecked` 把"各分量有限且
 *     `squared_length(v) > 0`"写成调用前提，违约即得脏结果；
 *  6. 比较：`equals` 是逐分量精确比较（`_mm_cmpeq_ps`，`NaN` 与任何值都不相等）；`near_equal` 用
 *     "绝对 + 相对"双容差，判定式写在其文档块里，不提供默认容差，**没有**全局 epsilon；
 *     **`/fp:fast` / `-ffast-math` 的边界**：这类选项允许编译器假定"不存在 NaN"，从而把
 *     `x == x` 折成恒真（MSVC `/fp:fast` 实测会在 `vec2` / `vec3` 实例化上这样做）。因此
 *     `equals` / `near_equal` 的 NaN 判定在 fast-math 配置下**不构成契约**：需要"非有限值分类"
 *     时只能用 `try_*` 入口里的位模式前检（D0-05），那一条不会被 fast-math 消除。此边界不影响
 *     有限输入之间的比较结果；
 *  7. 精度：`dot` 走 128-bit lane 的**成对**归约（`(l0+l2)+(l1+l3)`），与 `ID.Math.Scalar` 的升序
 *     累加顺序不同，两者结果可能差 ULP 量级；按 double oracle + 容差判定，不按 bitwise 等价（D0-05）。
 *     不提供 FMA / `approx_rsqrt` 一类近似入口，默认运算保持可预测精度；`length` / `try_normalize` /
 *     `normalize_unchecked` 走 `std::sqrt` 与其倒数（**不**用 `_mm_rsqrt_ps` 估计）；
 *  8. `constexpr`：intrinsic 在 C++20 下不是常量表达式，因此本头的运算入口都**不是** `constexpr`
 *     （存储层与访问面仍是 `constexpr`）；这不是精度或语义差异，只影响能否在编译期求值；
 *  9. 全部入口 `noexcept`、无分配、无 RTTI、无异常、无表达式模板。
 */

namespace ID::Math::SIMD
{
    /**
     *  @brief 逐分量相加
     *  @param lhs 左操作数
     *  @param rhs 右操作数
     *  @return 新向量，第 `i` 个分量为 `lhs[i] + rhs[i]`；不改写任何入参
     *  @note 只对 `T == float` 且 `N ∈ {2,3,4}` 存在；其它组合没有本运算符。
     *        `vec3` 结果的 padding lane 被固定写 `+0.0f`，入参 padding lane 不参与运算。
     */
    template <typename T, size_t N>
        requires detail::is_math_operand_v<T, N>
    ID_FORCEINLINE vector<T, N> operator+(const vector<T, N>& lhs, const vector<T, N>& rhs) noexcept
    {
        vector<T, N> result;
        detail::store_lanes<N>(result.data(),
                               _mm_add_ps(detail::load_lanes<N>(lhs.data()), detail::load_lanes<N>(rhs.data())));
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
        requires detail::is_math_operand_v<T, N>
    ID_FORCEINLINE vector<T, N> operator-(const vector<T, N>& lhs, const vector<T, N>& rhs) noexcept
    {
        vector<T, N> result;
        detail::store_lanes<N>(result.data(),
                               _mm_sub_ps(detail::load_lanes<N>(lhs.data()), detail::load_lanes<N>(rhs.data())));
        return result;
    }

    /**
     *  @brief 向量乘标量：`v * scalar`
     *  @param v 被乘向量
     *  @param scalar 乘数
     *  @return 新向量，第 `i` 个分量为 `v[i] * scalar`
     *  @note 标量按 `T`（即 `float`）取参：`int` / `double` 实参只做一次到 `float` 的标准转换，
     *        运算全程在 `float` 内完成，不产生 `double` 中间结果。没有 `operator*(vector, vector)`：
     *        逐分量积用 `hadamard` 显式命名。
     */
    template <typename T, size_t N>
        requires detail::is_math_operand_v<T, N>
    ID_FORCEINLINE vector<T, N> operator*(const vector<T, N>& v, T scalar) noexcept
    {
        vector<T, N> result;
        detail::store_lanes<N>(result.data(),
                               _mm_mul_ps(detail::load_lanes<N>(v.data()), _mm_set1_ps(scalar)));
        return result;
    }

    /**
     *  @brief 标量乘向量：`scalar * v`（与 `v * scalar` 同义）
     *  @param scalar 乘数
     *  @param v 被乘向量
     *  @return 新向量，第 `i` 个分量为 `scalar * v[i]`
     */
    template <typename T, size_t N>
        requires detail::is_math_operand_v<T, N>
    ID_FORCEINLINE vector<T, N> operator*(T scalar, const vector<T, N>& v) noexcept
    {
        vector<T, N> result;
        detail::store_lanes<N>(result.data(),
                               _mm_mul_ps(_mm_set1_ps(scalar), detail::load_lanes<N>(v.data())));
        return result;
    }

    /**
     *  @brief 向量除以标量：`v / scalar`
     *  @param v 被除数向量
     *  @param scalar 除数；**前提** 有限且不等于 0
     *  @return 新向量，第 `i` 个分量为 `v[i] / scalar`
     *  @note 本入口不做检查、不返回错误码：除数为 0 时得到 `inf` / `NaN` 分量，属调用方违约。
     *        用 `_mm_div_ps`（真除法），**不**用倒数近似：默认运算保持可预测精度。
     */
    template <typename T, size_t N>
        requires detail::is_math_operand_v<T, N>
    ID_FORCEINLINE vector<T, N> operator/(const vector<T, N>& v, T scalar) noexcept
    {
        vector<T, N> result;
        detail::store_lanes<N>(result.data(),
                               _mm_div_ps(detail::load_lanes<N>(v.data()), _mm_set1_ps(scalar)));
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
        requires detail::is_math_operand_v<T, N>
    ID_FORCEINLINE vector<T, N> hadamard(const vector<T, N>& lhs, const vector<T, N>& rhs) noexcept
    {
        vector<T, N> result;
        detail::store_lanes<N>(result.data(),
                               _mm_mul_ps(detail::load_lanes<N>(lhs.data()), detail::load_lanes<N>(rhs.data())));
        return result;
    }

    /**
     *  @brief 内积（点积）
     *  @param lhs 左操作数
     *  @param rhs 右操作数
     *  @return `Σ lhs[i] * rhs[i]`（`i` 从 `0` 到 `N-1`），按 128-bit lane 的**成对**顺序归约：
     *          先 4 个 lane 相乘，再 `(l0 + l2) + (l1 + l3)`
     *  @note 归约顺序与 `ID.Math.Scalar` 的升序累加不同，两者结果可能差 ULP 量级；精度按
     *        double oracle + 容差判定，不承诺与 Scalar bitwise 一致（D0-05）。
     *        `vec3` 的 `lane3` 是 padding：载入时已按位清零，因此它**不参与**求和，
     *        即使 padding 里原本是 `NaN` / `Inf` 也不会污染结果。
     */
    template <typename T, size_t N>
        requires detail::is_math_operand_v<T, N>
    T dot(const vector<T, N>& lhs, const vector<T, N>& rhs) noexcept
    {
        const __m128 products = _mm_mul_ps(detail::load_lanes<N>(lhs.data()), detail::load_lanes<N>(rhs.data()));
        const __m128 partial = _mm_add_ps(products, _mm_movehl_ps(products, products));
        const __m128 total = _mm_add_ss(partial, _mm_shuffle_ps(partial, partial, _MM_SHUFFLE(1, 1, 1, 1)));
        return _mm_cvtss_f32(total);
    }

    /**
     *  @brief 长度平方
     *  @param v 输入向量
     *  @return `dot(v, v)`
     *  @note 先平方再开方会多一次 `sqrt`，需要比较"离原点多远"时优先用本函数。
     */
    template <typename T, size_t N>
        requires detail::is_math_operand_v<T, N>
    ID_FORCEINLINE T squared_length(const vector<T, N>& v) noexcept
    {
        return dot(v, v);
    }

    /**
     *  @brief 欧几里得长度
     *  @param v 输入向量；**前提** 各分量有限，且 `squared_length(v)` 不溢出到非有限
     *  @return `sqrt(squared_length(v))`；前提被违反时结果可能是 `inf` / `NaN`
     *  @note 走 `std::sqrt`，**不**使用 `_mm_sqrt_ss` 之外的近似指令（如 `_mm_rsqrt_ps` 的估计路径）：
     *        默认运算保持可预测精度。
     */
    template <typename T, size_t N>
        requires detail::is_math_operand_v<T, N>
    T length(const vector<T, N>& v) noexcept
    {
        return std::sqrt(squared_length(v));
    }

    /**
     *  @brief 三维叉积
     *  @param lhs 左操作数
     *  @param rhs 右操作数
     *  @return `(lhs.y*rhs.z - lhs.z*rhs.y, lhs.z*rhs.x - lhs.x*rhs.z, lhs.x*rhs.y - lhs.y*rhs.x)`
     *  @note **只对** `vector<float, 3>` 存在（`N == 3` 是硬约束）。用 `_mm_shuffle_ps` 把两份操作数
     *        各自重排成 `yzx` / `zxy`，再做两次 4 路乘减：lane `0..2` 是叉积的三个分量，
     *        lane3 是 padding 的副产物，写回前被掩码清零。二维叉积用另名 `cross2`，
     *        不把二维情形塞进本函数，也不复用本语义。
     */
    template <typename T, size_t N>
        requires detail::is_math_operand_v<T, N> && (N == 3)
    ID_FORCEINLINE vector<T, N> cross(const vector<T, N>& lhs, const vector<T, N>& rhs) noexcept
    {
        const __m128 a = detail::load_lanes<N>(lhs.data());
        const __m128 b = detail::load_lanes<N>(rhs.data());

        const __m128 a_yzx = _mm_shuffle_ps(a, a, _MM_SHUFFLE(3, 0, 2, 1));
        const __m128 a_zxy = _mm_shuffle_ps(a, a, _MM_SHUFFLE(3, 1, 0, 2));
        const __m128 b_yzx = _mm_shuffle_ps(b, b, _MM_SHUFFLE(3, 0, 2, 1));
        const __m128 b_zxy = _mm_shuffle_ps(b, b, _MM_SHUFFLE(3, 1, 0, 2));

        vector<T, N> result;
        detail::store_lanes<N>(result.data(),
                               _mm_sub_ps(_mm_mul_ps(a_yzx, b_zxy), _mm_mul_ps(a_zxy, b_yzx)));
        return result;
    }

    /**
     *  @brief 二维叉积的标量形式（有向面积的两倍，即三维叉积的 z 分量）
     *  @param lhs 左操作数
     *  @param rhs 右操作数
     *  @return `lhs[0] * rhs[1] - lhs[1] * rhs[0]`
     *  @note **只对** `vector<float, 2>` 存在。返回值是标量而不是向量，因此它不是三维 `cross`
     *        的特化，也不复用它的语义：需要二维"垂直向量"时必须由调用方显式构造。
     *        本函数只消费两个逻辑 lane 并产出一个标量，属于跨 lane 的标量表达式，因此直接写
     *        `lhs[0] * rhs[1] - lhs[1] * rhs[0]`（走 `operator[]` 的逻辑分量访问），**不**为本函数
     *        拼一个 128-bit 载入——那样对 `vec2` 只会引入 16 字节访问的风险，没有任何收益。
     */
    template <typename T, size_t N>
        requires detail::is_math_operand_v<T, N> && (N == 2)
    ID_FORCEINLINE T cross2(const vector<T, N>& lhs, const vector<T, N>& rhs) noexcept
    {
        return lhs[0] * rhs[1] - lhs[1] * rhs[0];
    }

    /**
     *  @brief 归一化的 unchecked 入口（热路径）
     *  @param v 源向量；**前提** 各分量有限，且 `squared_length(v) > 0`
     *  @return `v / length(v)`（实现为 `v * (1 / sqrt(squared_length(v)))`）
     *  @note 本入口不做任何检查、不返回错误码：零向量会得到 `inf` / `NaN` 分量（`1/0`），
     *        非有限输入会得到脏结果，两者都属违约；需要"失败时不改输出"时用 `try_normalize`。
     *        不承诺 bitwise 精度：`-ffast-math` / `/fp:fast` 下 `1/sqrt` 可能被改写成倒数平方根
     *        估计加迭代，结果与 double oracle 可差 ULP 量级；判定按容差，不按 bitwise 等价。
     */
    template <typename T, size_t N>
        requires detail::is_math_operand_v<T, N>
    vector<T, N> normalize_unchecked(const vector<T, N>& v) noexcept
    {
        const T inverse_length = T{1} / std::sqrt(squared_length(v));

        vector<T, N> result;
        detail::store_lanes<N>(result.data(),
                               _mm_mul_ps(detail::load_lanes<N>(v.data()), _mm_set1_ps(inverse_length)));
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
        requires detail::is_math_operand_v<T, N>
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
     *  @return 全部**逻辑**分量满足 `lhs[i] == rhs[i]` 时为 `true`
     *  @note 这是 `==` 语义的精确比较，不含任何容差：`-0.0f` 与 `0.0f` 相等，`NaN` 与任何值
     *        （含自身）都不相等（`_mm_cmpeq_ps` 对 `NaN` 恒为假）。需要容差时用 `near_equal`。
     *        比较结果先与逻辑 lane 掩码相与，因此 `vec3` 的 padding lane 不参与判定。
     *  @note `/fp:fast` / `-ffast-math` 允许编译器假定不存在 `NaN`，`x == x` 可能被折成恒真；
     *        "`NaN` 不等于自身"这一条**不在这类配置的契约内**（有限输入之间的比较结果不受影响）。
     *        需要非有限值分类时用 `try_normalize` 一类的位模式前检入口。
     */
    template <typename T, size_t N>
        requires detail::is_math_operand_v<T, N>
    ID_FORCEINLINE bool equals(const vector<T, N>& lhs, const vector<T, N>& rhs) noexcept
    {
        const __m128 equal = _mm_cmpeq_ps(detail::load_lanes<N>(lhs.data()), detail::load_lanes<N>(rhs.data()));
        return _mm_movemask_ps(_mm_and_ps(equal, detail::lane_mask<N>())) == detail::lane_presence_bits_v<N>;
    }

    /**
     *  @brief 逐分量"绝对 + 相对"双容差比较
     *  @param lhs 左操作数；**前提** 各分量有限
     *  @param rhs 右操作数；**前提** 各分量有限
     *  @param abs_tolerance 绝对容差；**前提** 有限且 `>= 0`，用于"接近 0"的量级
     *  @param rel_tolerance 相对容差；**前提** 有限且 `>= 0`，按参与比较的量级放大，用于"大数"
     *  @return 全部**逻辑**分量满足下式时为 `true`：
     *          `|lhs[i] - rhs[i]| <= abs_tolerance + rel_tolerance * max(|lhs[i]|, |rhs[i]|)`
     *  @note 判定式逐分量写死在上面的注释与实现里，**没有**内部默认 epsilon：容差必须由调用方按
     *        算法与量级给出（例如几何量常用 `abs_tolerance = 1e-6f`、`rel_tolerance = 1e-5f`，
     *        这只是示例，不是精度承诺）。用 `max(|a|, |b|)` 而不是单侧量级，使判定对称。
     *        `NaN` 参与比较时按 `<=` 的语义返回 `false`（契约外的非有限输入；该结论在
     *        `/fp:fast` / `-ffast-math` 下同样不构成契约，见文件级第 6 条）。
     *        四个量都在 128-bit lane 上按逐分量顺序求值，`vec3` 的 padding lane 由掩码排除。
     */
    template <typename T, size_t N>
        requires detail::is_math_operand_v<T, N>
    ID_FORCEINLINE bool near_equal(const vector<T, N>& lhs, const vector<T, N>& rhs,
                                  T abs_tolerance, T rel_tolerance) noexcept
    {
        const __m128 lhs_lanes = detail::load_lanes<N>(lhs.data());
        const __m128 rhs_lanes = detail::load_lanes<N>(rhs.data());
        const __m128 sign_bits = detail::sign_bits_mask();

        // `_mm_andnot_ps(sign_bits, x)` 清掉符号位，即 IEEE-754 的 |x|。
        const __m128 lhs_magnitude = _mm_andnot_ps(sign_bits, lhs_lanes);
        const __m128 rhs_magnitude = _mm_andnot_ps(sign_bits, rhs_lanes);
        const __m128 difference = _mm_andnot_ps(sign_bits, _mm_sub_ps(lhs_lanes, rhs_lanes));
        const __m128 magnitude = _mm_max_ps(lhs_magnitude, rhs_magnitude);
        const __m128 limit = _mm_add_ps(_mm_set1_ps(abs_tolerance), _mm_mul_ps(_mm_set1_ps(rel_tolerance), magnitude));

        const __m128 within = _mm_cmple_ps(difference, limit);
        return _mm_movemask_ps(_mm_and_ps(within, detail::lane_mask<N>())) == detail::lane_presence_bits_v<N>;
    }

    /**
     *  @brief 把逻辑分量写入调用方提供的连续缓冲（跨 backend 转换与外部序列化的许可形式之一）
     *  @param v 源向量
     *  @param out 目标缓冲；**前提** 至少 `N` 个 `float` 可写
     *  @note 只写 `N` 个值、**不写** padding lane：`vec3` 的缓冲只需要 3 个 `float`，
     *        `vec2` 的缓冲只需要 2 个 `float`（不会因为 SIMD 而要求 4 个）。
     *        写入的是逻辑分量本身，因此格式与 `ID.Math.Scalar` 的同名约定一致，可用来做显式的
     *        跨 backend 转换；**不得**用 `memcpy` 直接搬内部布局（padding 与 ABI 尾部填充不是格式）。
     */
    template <typename T, size_t N>
        requires detail::is_math_operand_v<T, N>
    ID_FORCEINLINE void pack(const vector<T, N>& v, float* out) noexcept
    {
        if constexpr (N == 4)
        {
            // 4 个逻辑分量恰好铺满 16 字节，一次非对齐 store 就够（目标缓冲没有对齐前提）。
            _mm_storeu_ps(out, detail::load_lanes<4>(v.data()));
        }
        else
        {
            for (size_t i = 0; i < N; ++i)
            {
                out[i] = v[i];
            }
        }
    }

    /**
     *  @brief 从连续缓冲读入逻辑分量（`pack` 的逆操作）
     *  @param src 源缓冲；**前提** 至少 `N` 个 `float` 可读
     *  @param out 目标向量；写入后逻辑分量为 `src[0..N-1]`，padding lane 为 `+0.0f`
     *  @note 先把结果算进临时对象再一次性赋值，因此 `&out == &v` 一类别名场景安全；
     *        目标对象的 padding lane 由默认构造保证为零，源缓冲的越界部分从不读取。
     */
    template <typename T, size_t N>
        requires detail::is_math_operand_v<T, N>
    ID_FORCEINLINE void unpack(const float* src, vector<T, N>& out) noexcept
    {
        vector<T, N> result;
        if constexpr (N == 4)
        {
            detail::store_lanes<4>(result.data(), _mm_loadu_ps(src));
        }
        else
        {
            for (size_t i = 0; i < N; ++i)
            {
                result[i] = src[i];
            }
        }
        out = result;
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
} // namespace ID::Math::SIMD
