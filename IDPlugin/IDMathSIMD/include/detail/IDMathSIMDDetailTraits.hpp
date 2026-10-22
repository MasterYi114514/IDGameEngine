#pragma once

#include <cstdint>
#include <cstddef>
#include <type_traits>

#include "Kernel/CoreTypes.hpp"

/**
 *  ID.Math.SIMD 的私有编译期工具（detail 目录不属于对外承诺）
 *  1. 只放模板约束、容量判据与存储槽位规则，不放存储、运算或任何状态；
 *  2. 下游只应包含伞头 `IDMathSIMDApi.hpp`；本头随实现调整，不承诺稳定性；
 *  3. 存储层与计算层分离（计划书 §5.2）：本头的 `is_valid_component_v` 等判据只决定"存储是否
 *     存在"，运算的收窄判据 `is_math_operand_v` 是运算头唯一的收窄出处，二者不得混用——
 *     `vector<int, 3>` 存储可用，但任何运算都必须编译失败；
 *  4. `ComponentBytes` 偏特化存在的唯一理由：constraints 里不能对非算术类型求 `sizeof`
 *     （那是硬错误），而 `vector<void, N>` 这类误用必须得到"约束不满足"的具体诊断。
 */

namespace ID::Math::SIMD::detail
{
    /**
     *  @brief 元素类型字节数；非算术类型退化为 1
     *  @tparam T 待测类型
     *  @note 只有算术类型的偏特化会真正求 `sizeof`：让"元素类型非法"这一条在 constraints
     *        里先失败，而不是让 `sizeof` 提前产生硬错误。
     */
    template <typename T, bool = std::is_arithmetic_v<T>>
    struct ComponentBytes
    {
        static inline constexpr size_t value = 1;
    };

    template <typename T>
    struct ComponentBytes<T, true>
    {
        static inline constexpr size_t value = sizeof(T);
    };

    /**
     *  @brief 合法的 `vector` 元素类型
     *  @note `bool` 被排除：`bool` 不是"每个分量可独立取址"的存储元素；cv 限定被排除：
     *        `vector<const float, N>` 会造出既不可写又可被按值拷贝的类型，语义无法定义。
     */
    template <typename T>
    inline constexpr bool is_valid_component_v = std::is_arithmetic_v<T>
                                                 && !std::is_same_v<T, bool>
                                                 && !std::is_const_v<T>
                                                 && !std::is_volatile_v<T>;

    /// 单个 `vector` 的逻辑分量数上限（实现资源上限，不是占位符）
    inline constexpr size_t kMaxComponentCount = 256;

    /// 逻辑分量数非 0：`vector<T, 0>` 没有可寻址分量，也没有任何合法运算语义
    template <size_t N>
    inline constexpr bool is_nonzero_component_count_v = (N > 0);

    /**
     *  @brief `sizeof(T) × N` 不溢出 `size_t`
     *  @tparam T 元素类型：只在 `is_valid_component_v<T>` 为真时有意义
     *  @tparam N 逻辑分量数
     *  @note 在上限之内它不可能失败，但"乘法溢出必须在编译期拒绝"需要一条可执行判据：
     *        上限若被调高，本条件是唯一的溢出防线。
     */
    template <typename T, size_t N>
    inline constexpr bool is_non_overflowing_extent_v = (N <= (SIZE_MAX / ComponentBytes<T>::value));

    /// 逻辑分量数不超实现上限
    template <size_t N>
    inline constexpr bool is_within_component_cap_v = (N <= kMaxComponentCount);

    /**
     *  @brief 物理存储槽位数 `StorageN`：SIMD backend 的 padding 规则（D0-02）
     *  @tparam N 逻辑分量数
     *  @return `N <= 2` 时为 `N`（2 分量向量不需要再补 lane）；否则向上取整到 4 的倍数
     *          （128-bit lane 的 4 个 float 槽位）。因此 `vec2 -> 2`、`vec3 -> 4`、`vec4 -> 4`，
     *        `vector<float, 5> -> 8`。
     *  @note 本规则**只看 `N`、不看 `sizeof(T)`**：D0-02 只冻结了 `float` 的 `vec2` / `vec3` /
     *        `vec4` 三个布局，其它 `T` / `N` 组合的存储同样可用，但没有任何运算承诺。
     *        存储槽位数属于公开布局（`kStorageCount`），不得改成运行期可变的量。
     */
    template <size_t N>
    inline constexpr size_t storage_count_v = (N <= 2) ? N : (((N + 3) / 4) * 4);

    /**
     *  @brief 运算操作数的合法组合
     *  @note 计划书 §5.4 冻结的运算收窄是"`T` 恰为 `float`、`N ∈ {2,3,4}`"；SIMD 运算头的
     *        `requires` 必须引用本判据，不得各自另写一份。存储层不受它约束：
     *        `vector<int, 3>` / `vector<float, 8>` 存储可用，但按其运算必须编译失败。
     */
    template <typename T, size_t N>
    inline constexpr bool is_math_operand_v = std::is_same_v<T, float> && (N >= 2) && (N <= 4);

    /// `matrix` 的逻辑维度上限：v1 数学维度就是 `2 / 3 / 4`（计划书 §6.1），不提供更大规模
    inline constexpr size_t kMaxMatrixExtent = 4;

    /**
     *  @brief 合法的 `matrix` 逻辑维度
     *  @tparam Extent 逻辑行数或列数
     *  @note `matrix<Rows, Cols>` 的 `Rows` / `Cols` 都必须落在 `[2, kMaxMatrixExtent]`：v1 只承诺
     *        这 9 种形状（含矩形）的存储与运算，更大规模属于后续专门方案，不在此留"尽力支持"的
     *        半成品路径；不满足时 `matrix` 的 `requires` 直接失败，不会生成一个存储可用但语义
     *        未定义的胖类型。
     */
    template <size_t Extent>
    inline constexpr bool is_valid_matrix_extent_v = (Extent >= 2) && (Extent <= kMaxMatrixExtent);
} // namespace ID::Math::SIMD::detail
