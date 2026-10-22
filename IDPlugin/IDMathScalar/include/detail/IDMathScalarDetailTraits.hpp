#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "Kernel/CoreTypes.hpp"

/**
 *  ID.Math.Scalar 的私有编译期工具（detail 目录不属于对外承诺）
 *  1. 只放模板约束与容量判据，不放存储、运算或任何状态；
 *  2. 下游只应包含伞头 `IDMathScalarApi.hpp`；本头随实现调整，不承诺稳定性；
 *  3. `kMaxComponentCount` 是**实现资源上限**（不是占位符）：超过它的实例化在编译期被
 *     constraints 拒绝，不存在运行期扩容、截断或降级路径；
 *  4. `ComponentBytes` 偏特化存在的唯一理由：constraints 里不能对非算术类型求 `sizeof`
 *     （那是硬错误），而 `vector<void, N>` 这类误用必须得到"约束不满足"的具体诊断。
 */

namespace ID::Math::Scalar::detail
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
     *  @brief 运算层允许的 `(T, N)` 组合：`T` 恰为 `float` 且 `N ∈ {2,3,4}`
     *  @tparam T 元素类型
     *  @tparam N 逻辑分量数
     *  @note 存储层对所有合法 `T`/`N` 可用，运算层只对这个组合存在：`vector<int,3>`、
     *        `vector<float,8>` 之类的实例化**没有**运算符与自由函数，调用即编译期失败；
     *        这里没有标量 fallback，也没有隐式提升到 `double` 的路径。
     */
    template <typename T, size_t N>
    inline constexpr bool is_supported_math_shape_v = std::is_same_v<T, float>
                                                     && ((N == 2) || (N == 3) || (N == 4));
} // namespace ID::Math::Scalar::detail
