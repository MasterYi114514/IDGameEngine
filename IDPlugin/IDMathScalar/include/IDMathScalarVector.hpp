#pragma once

#include <cstddef>
#include <type_traits>

#include "Common/Core.hpp"
#include "Kernel/CoreTypes.hpp"

#include "detail/IDMathScalarDetailTraits.hpp"

/**
 *  ID.Math.Scalar 的 `vector<T, N>`：存储层与访问面（不含运算）
 *  1. 唯一数据载体是 `T storage[N]`：Scalar backend 的 `StorageN == N`，**padding 恒为 0 字节**，
 *     没有逻辑分量之外的 lane；`data_len() == N`，`storage_bytes() == N * sizeof(T)`；
 *  2. 存储层对所有合法 `T` / `N` 可用（`T` 为算术类型且非 `bool`、非 cv；`0 < N <= 实现上限`）；
 *     `T == float` 且 `N ∈ {2,3,4}` 的运算收窄属于运算头，不是存储层职责；
 *  3. `storage` 是唯一可写状态：禁止 `union` punning、禁止重复存储 `x/y/z/w`、禁止表达式模板；
 *     `x()` / `y()` / `z()` / `w()` 只是 `storage` 的具名视图，不新增存储、不改变布局；
 *  4. 布局契约（`sizeof` / `alignof` / `trivially copyable` / `standard layout`）由本头末尾的
 *     `static_assert` 钉住；调用方不得靠 `sizeof` 猜布局，也不得假设与 SIMD backend 的同名类型同布局。
 */

namespace ID::Math::Scalar
{
    /**
     *  @brief 紧凑定长向量（列向量约定，供几何与线性代数使用）
     *  @tparam T 元素类型：算术类型，非 `bool`、非 cv 限定
     *  @tparam N 逻辑分量数：大于 0、不超实现上限，且 `sizeof(T) * N` 不溢出
     *  @note 默认构造零初始化（这是契约，不是"零成本"结论）；不提供 `uninitialized` tag；
     *        拷贝/赋值是隐式生成的成员语义，因此对象保持可平凡复制；对象内没有第二份可写状态。
     */
    template <typename T, size_t N>
        requires detail::is_valid_component_v<T> && detail::is_nonzero_component_count_v<N>
                 && detail::is_non_overflowing_extent_v<T, N> && detail::is_within_component_cap_v<N>
    class vector
    {
    public:
        /// 逻辑分量数（= 模板参数 `N`）
        static inline constexpr size_t kComponentCount = N;

        /// 物理存储槽位数：Scalar backend 恒等于逻辑分量数（没有 padding lane）
        static inline constexpr size_t kStorageCount = N;

        /**
         *  @brief 默认构造：全部分量零初始化
         *  @note 零初始化是公开契约（D0-03）；`uninitialized` tag 不在本版接口内，
         *        也不得把本构造的代价描述成"零开销"。
         */
        ID_FORCEINLINE constexpr vector() noexcept
            : storage{}
        {
        }

        /**
         *  @brief 逐分量构造：恰好 `N` 个实参，按顺序对应 `storage[0..N-1]`
         *  @param values 分量值；个数必须等于 `N`，且每个都能转换为 `T`
         *  @note `explicit`：不做隐式转换。实参个数不等于 `N` 时本构造函数不参与重载决议，
         *        因此"少给或多给分量"是编译期错误，不存在默认补齐。
         */
        template <typename... Ts>
            requires ((sizeof...(Ts) == N) && (std::is_convertible_v<Ts, T> && ...))
        ID_FORCEINLINE constexpr explicit vector(Ts... values) noexcept
            : storage{ static_cast<T>(values)... }
        {
        }

        /**
         *  @brief 跨类型逐分量转换：`vector<U, N>` -> `vector<T, N>`
         *  @tparam U 源元素类型：同样必须是合法元素类型
         *  @param other 源向量；逻辑分量数必须相同
         *  @note `explicit`：不做隐式提升或降级（`float` -> `double` 也不得隐式发生），
         *        每一维都走一次显式 `static_cast`。
         */
        template <typename U>
            requires detail::is_valid_component_v<U> && (!std::is_same_v<U, T>)
        ID_FORCEINLINE constexpr explicit vector(const vector<U, N>& other) noexcept
        {
            // 故意不写 `: storage{}`：转换会把每个槽位都赋值，先零填充再覆盖等于双次写。
            // 未出现在 mem-initializer 里的数组成员只是"默认初始化"，本构造函数体内无一遗漏地赋值。
            for (size_t i = 0; i < kStorageCount; ++i)
            {
                storage[i] = static_cast<T>(other[i]);
            }
        }

        /**
         *  @brief 单值填充：全部逻辑分量写同一个值
         *  @param value 填充值
         *  @return 新的 `vector`；不修改任何既有对象
         */
        ID_FORCEINLINE static constexpr vector splat(T value) noexcept
        {
            vector result;
            for (size_t i = 0; i < kStorageCount; ++i)
            {
                result.storage[i] = value;
            }
            return result;
        }

        /**
         *  @brief 首元素地址（可写）
         *  @return 指向 `storage[0]` 的指针；其后连续 `N` 个 `T`
         *  @note 长度由 `data_len()` 给出，**不是** `sizeof(vector)`；SIMD backend 的 `data()`
         *        可能带 padding lane，因此两个 backend 的指针不得互相重解释。
         */
        ID_FORCEINLINE constexpr T* data() noexcept
        {
            return storage;
        }

        /**
         *  @brief 首元素地址（只读）
         *  @return 指向 `storage[0]` 的指针；其后连续 `N` 个 `const T`
         */
        ID_FORCEINLINE constexpr const T* data() const noexcept
        {
            return storage;
        }

        /// 经 `data()` 可读的逻辑分量数：Scalar backend 恒为 `N`
        ID_FORCEINLINE constexpr size_t data_len() const noexcept
        {
            return kStorageCount;
        }

        /// 存储字节数：`N * sizeof(T)`，恒等于 `sizeof(vector)`（没有 padding）
        ID_FORCEINLINE constexpr size_t storage_bytes() const noexcept
        {
            return sizeof(storage);
        }

        /**
         *  @brief 无越界检查的分量访问（热路径）
         *  @param index 分量下标；**前提** `index < N`，越界即未定义行为
         *  @return 该分量引用
         *  @note 本入口不做检查、不做钳制、不返回错误码：需要在异常输入下安全时必须由调用方
         *        先判界。用越界下标读取不存在的逻辑分量同样是契约外行为。
         */
        ID_FORCEINLINE constexpr T& operator[](size_t index) noexcept
        {
            return storage[index];
        }

        /// 只读分量访问；前提与 `operator[]` 的非 const 版本相同
        ID_FORCEINLINE constexpr const T& operator[](size_t index) const noexcept
        {
            return storage[index];
        }

        // 具名分量视图：只是 storage 的别名，不新增存储、不改变布局。
        // 越出 N 的分量在任何实例化里都不存在（y/z/w 由 requires 逐级收窄）。

        /// 第 0 维；所有合法 `N` 都有
        ID_FORCEINLINE constexpr T& x() noexcept
        {
            return storage[0];
        }

        /// 第 0 维（只读）
        ID_FORCEINLINE constexpr const T& x() const noexcept
        {
            return storage[0];
        }

        /// 第 1 维；仅 `N >= 2` 存在
        ID_FORCEINLINE constexpr T& y() noexcept
            requires (N >= 2)
        {
            return storage[1];
        }

        /// 第 1 维（只读）；仅 `N >= 2` 存在
        ID_FORCEINLINE constexpr const T& y() const noexcept
            requires (N >= 2)
        {
            return storage[1];
        }

        /// 第 2 维；仅 `N >= 3` 存在
        ID_FORCEINLINE constexpr T& z() noexcept
            requires (N >= 3)
        {
            return storage[2];
        }

        /// 第 2 维（只读）；仅 `N >= 3` 存在
        ID_FORCEINLINE constexpr const T& z() const noexcept
            requires (N >= 3)
        {
            return storage[2];
        }

        /// 第 3 维；仅 `N >= 4` 存在
        ID_FORCEINLINE constexpr T& w() noexcept
            requires (N >= 4)
        {
            return storage[3];
        }

        /// 第 3 维（只读）；仅 `N >= 4` 存在
        ID_FORCEINLINE constexpr const T& w() const noexcept
            requires (N >= 4)
        {
            return storage[3];
        }

    private:
        // 唯一数据载体：槽位数等于逻辑分量数（Scalar 紧凑，没有 padding lane）
        T storage[kStorageCount];
    };

    // 公开的 float 几何别名。小写是本 backend 公开头的命名特例（仅 vector / matrix / vec2 / vec3 / vec4），
    // 别名只改变名字，不新增布局或语义；不得引出 float2 / float3 / float4 之类的额外名字。
    using vec2 = vector<float, 2>;
    using vec3 = vector<float, 3>;
    using vec4 = vector<float, 4>;

    // 布局契约。断言必须放在类型完整之后：类体内对自身求 sizeof / is_trivially_copyable 都是
    // 非法表达式（不完整类型），所以契约钉在类外的公开别名上。
    static_assert(sizeof(vec2) == 8,  "Scalar 的 vec2 必须是 8 字节（2 × float，无 padding）");
    static_assert(sizeof(vec3) == 12, "Scalar 的 vec3 必须是 12 字节（3 × float，无 padding）");
    static_assert(sizeof(vec4) == 16, "Scalar 的 vec4 必须是 16 字节（4 × float，无 padding）");
    static_assert(alignof(vec2) == alignof(float), "Scalar 的 vec2 对齐必须等于元素对齐");
    static_assert(alignof(vec3) == alignof(float), "Scalar 的 vec3 对齐必须等于元素对齐");
    static_assert(alignof(vec4) == alignof(float), "Scalar 的 vec4 对齐必须等于元素对齐");
    static_assert(std::is_trivially_copyable_v<vec2> && std::is_trivially_copyable_v<vec3>
                      && std::is_trivially_copyable_v<vec4>,
                  "vector 必须可平凡复制：按值跨模块传递不得依赖任何非平凡语义");
    static_assert(std::is_standard_layout_v<vec2> && std::is_standard_layout_v<vec3>
                      && std::is_standard_layout_v<vec4>,
                  "vector 必须是 standard layout：布局契约不接受编译器自行插入的成员");
} // namespace ID::Math::Scalar
