#pragma once

#include <cstddef>
#include <type_traits>

#include "Common/Core.hpp"
#include "Kernel/CoreTypes.hpp"

#include "detail/IDMathSIMDIsaGate.hpp"
#include "detail/IDMathSIMDDetailTraits.hpp"

/**
 *  ID.Math.SIMD 的 `vector<T, N>`：存储层与访问面（不含运算）
 *  1. 唯一数据载体是 `T storage[StorageN]`（代码里的 `kStorageCount`），逻辑分量数是 `N`。SIMD
 *     backend 的 `StorageN` 由 `N` 决定（`N <= 2` 时为 `N`，否则向上取整到 4 的倍数）：
 *     `vec2 -> 2`、`vec3 -> 4`、`vec4 -> 4`；整个类型 `alignas(16)`（128-bit lane 对齐，D0-02）；
 *  2. 存储层对所有合法 `T` / `N` 可用（`T` 为算术类型且非 `bool`、非 cv；`0 < N <= 实现上限`）；
 *     `T == float` 且 `N ∈ {2,3,4}` 的运算收窄属于运算头（`IDMathSIMDVectorOps.hpp` 与
 *     `IDMathSIMDMatrix.hpp`），不是存储层职责——本头**不含任何运算**，也不做标量 fallback；
 *  3. `storage` 是唯一可写状态：禁止 `union` punning、禁止重复存储 `x/y/z/w`、禁止表达式模板；
 *     `x()` / `y()` / `z()` / `w()` 只是 `storage` 的具名视图，不新增存储、不改变布局；
 *     intrinsic 值（后续运算）只作临时寄存器与计算中间值，不是第二份持久数据源；
 *  4. padding 是**公开布局的一部分**，语义见类文档块与 `README.md`；布局契约
 *     （`sizeof` / `alignof` / 存储槽位数 / `trivially copyable` / `standard layout`）由本头末尾的
 *     `static_assert` 钉住；调用方不得靠 `sizeof` 猜布局，也不得假设与 Scalar backend 的同名类型
 *     同布局（跨 backend 只允许显式逐元素转换，禁止 `reinterpret_cast`）。
 */

namespace ID::Math::SIMD
{
    /// 本 backend 向量存储的对齐字节数：128-bit lane（D0-02 冻结值，属于公开布局）
    inline constexpr size_t kVectorAlignmentBytes = 16;

    /**
     *  @brief 定长向量（列向量约定，供几何与线性代数使用）
     *  @tparam T 元素类型：算术类型，非 `bool`、非 cv 限定
     *  @tparam N 逻辑分量数：大于 0、不超实现上限，且 `sizeof(T) * N` 不溢出
     *
     *  @note **布局与 padding（公开契约）**：
     *        - 唯一成员是 `T storage[kStorageCount]`，`kStorageCount >= N`；
     *        - 声明出来的 padding lane 是 `storage[N .. kStorageCount-1]`（`kPaddingLaneCount` 个），
     *          它们**不属于公开值**：不是运算输入、不是运算结果、不参与 `dot` / `norm` 等聚合；
     *        - padding lane **参与构造与赋值**：四个构造入口（默认构造、逐分量构造、跨类型转换、
     *          `splat`）**无条件**把它们写成 `+0.0f`（位模式全零），不读取任何源值；隐式拷贝/移动
     *          构造与赋值按数组成员逐槽复制，因此由上述入口产生的对象在复制后 padding lane 仍为零
     *          （若调用方用 `memcpy` 把外来字节灌进对象表示，复制会原样带过去，那属于契约外用法）。
     *          零值只是**确定性**，不是"可以把 padding lane 当 0 参与运算"的许可：运算必须用掩码，
     *          掩码与加载不得越界；
     *        - 若 `kStorageCount * sizeof(T)` 不是 `kVectorAlignmentBytes` 的整数倍（例如 `vec2`：
     *          存储 8 字节、类型对齐 16 字节），`sizeof(vector)` 会向上取整到 16 的倍数，多出来的
     *          尾部字节是**ABI 尾部填充**：不属于任何成员、不可读、不保证清零，拷贝时是否复制由
     *          ABI 决定，代码不得依赖；
     *        - `data_len()` 是 `data()` 的可读长度（= `N`，逻辑分量数），`storage_bytes()` 是存储载体
     *          的字节数（= `kStorageCount * sizeof(T)`），二者都**不等于** `sizeof(vector)`
     *          （见 `README.md` 的布局表）。调用方不得用 `sizeof` 反推布局。
     *
     *  @note **构造与访问**：默认构造零初始化（D0-03，这是契约，不是"零开销"结论）；不提供
     *        `uninitialized` tag；拷贝/赋值是隐式生成的成员语义，因此对象保持可平凡复制；
     *        对象内没有第二份可写状态；所有成员函数 `noexcept`。
     */
    template <typename T, size_t N>
        requires detail::is_valid_component_v<T> && detail::is_nonzero_component_count_v<N>
                 && detail::is_non_overflowing_extent_v<T, N> && detail::is_within_component_cap_v<N>
    class alignas(kVectorAlignmentBytes) vector
    {
    public:
        /// 逻辑分量数（= 模板参数 `N`）
        static inline constexpr size_t kComponentCount = N;

        /// 物理存储槽位数：`N <= 2` 时为 `N`，否则向上取整到 4 的倍数（SIMD padding 规则）
        static inline constexpr size_t kStorageCount = detail::storage_count_v<N>;

        /// 尾部 padding lane 数：`kStorageCount - N`（`vec2` / `vec4` 为 0，`vec3` 为 1）
        static inline constexpr size_t kPaddingLaneCount = kStorageCount - N;

        /**
         *  @brief 默认构造：全部槽位零初始化
         *  @note 零初始化是公开契约（D0-03）；padding lane 同样被零初始化；
         *        `uninitialized` tag 不在本版接口内，也不得把本构造的代价描述成"零开销"。
         */
        ID_FORCEINLINE constexpr vector() noexcept
            : storage{}
        {
        }

        /**
         *  @brief 逐分量构造：恰好 `N` 个实参，按顺序对应 `storage[0..N-1]`
         *  @param values 分量值；个数必须等于 `N`，且每个都能转换为 `T`
         *  @note `explicit`：不做隐式转换。实参个数不等于 `N` 时本构造函数不参与重载决议，
         *        因此"少给或多给分量"是编译期错误，不存在默认补齐。剩余 padding lane 由数组的
         *        列表初始化补零，不读取源值。
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
         *        每一维都走一次显式 `static_cast`。padding lane 先由 `storage{}` 全零，
         *        再写前 `N` 个槽位：源对象的 padding lane 从不被读取。
         */
        template <typename U>
            requires detail::is_valid_component_v<U> && (!std::is_same_v<U, T>)
        ID_FORCEINLINE constexpr explicit vector(const vector<U, N>& other) noexcept
            : storage{}
        {
            for (size_t i = 0; i < kComponentCount; ++i)
            {
                storage[i] = static_cast<T>(other[i]);
            }
        }

        /**
         *  @brief 单值填充：全部逻辑分量写同一个值
         *  @param value 填充值
         *  @return 新的 `vector`；不修改任何既有对象
         *  @note padding lane 保持零，不随 `value` 填充：它们不属于公开值。
         */
        ID_FORCEINLINE static constexpr vector splat(T value) noexcept
        {
            vector result;
            for (size_t i = 0; i < kComponentCount; ++i)
            {
                result.storage[i] = value;
            }
            return result;
        }

        /**
         *  @brief 首元素地址（可写）
         *  @return 指向 `storage[0]` 的指针；其后**连续 `data_len()` 个 `T` 可读写**
         *  @note 对齐前提：对象按本类型要求对齐（`alignas(16)` 由类型保证，栈/成员/`new` 都满足），
         *        因此 `data()` 本身 16 字节对齐；对 `vec3` 从 `data()` 读 16 字节不越界。
         *        但**逻辑上可读长度仍是 `N`**：padding lane 不属于公开值，读它属于契约外用法。
         *        长度由 `data_len()` 给出，**不是** `sizeof(vector)`；SIMD 与 Scalar backend 的指针
         *        不得互相重解释。
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

        /// 经 `data()` 可读的逻辑分量数：恒为 `N`（padding lane 不计入）
        ID_FORCEINLINE constexpr size_t data_len() const noexcept
        {
            return kComponentCount;
        }

        /**
         *  @brief 存储载体字节数：`kStorageCount * sizeof(T)`
         *  @note 不含 ABI 尾部填充，因此可能**小于** `sizeof(vector)`：`vec2` 为 8（`sizeof` 是 16）、
         *        `vec3` / `vec4` 为 16（与 `sizeof` 相同）。`storage_bytes()` 描述的是可寻址槽位，
         *        不是对象尺寸。
         */
        ID_FORCEINLINE constexpr size_t storage_bytes() const noexcept
        {
            return sizeof(storage);
        }

        /**
         *  @brief 无越界检查的分量访问（热路径）
         *  @param index 分量下标；**前提** `index < N`，越界即未定义行为
         *  @return 该分量引用
         *  @note 本入口不做检查、不做钳制、不返回错误码：需要在异常输入下安全时必须由调用方先判界。
         *        用 `index ∈ [N, kStorageCount)` 读取 padding lane 同样是契约外行为：该 lane 不承载
         *        公开值，即使物理可寻址也不得读取。
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
        // 唯一数据载体：槽位数由 N 决定（SIMD：N <= 2 时为 N，否则向上取整到 4 的倍数）。
        // 类型整体 alignas(16)：storage[0] 因此 16 字节对齐，后续 SSE2 加载不需要额外对齐分支。
        T storage[kStorageCount];
    };

    // 公开的 float 几何别名。小写是本 backend 公开头的命名特例（仅 vector / matrix / vec2 / vec3 / vec4），
    // 别名只改变名字，不新增布局或语义；不得引出 float2 / float3 / float4 之类的额外名字。
    using vec2 = vector<float, 2>;
    using vec3 = vector<float, 3>;
    using vec4 = vector<float, 4>;

    // 存储槽位规则与 padding lane 数：D0-02 冻结值的可执行形式。
    static_assert(vec2::kStorageCount == 2, "SIMD 的 vec2 必须是 2 个物理槽位");
    static_assert(vec3::kStorageCount == 4, "SIMD 的 vec3 必须是 4 个物理槽位（StorageN = 4，D0-02）");
    static_assert(vec4::kStorageCount == 4, "SIMD 的 vec4 必须是 4 个物理槽位");
    static_assert(vec2::kPaddingLaneCount == 0 && vec3::kPaddingLaneCount == 1 && vec4::kPaddingLaneCount == 0,
                  "尾部 padding lane 数必须是 0 / 1 / 0（只有 vec3 有声明出来的 padding lane）");
    static_assert(vector<float, 5>::kStorageCount == 8,
                  "N > 4 时存储槽位向上取整到 4 的倍数（该组合只有存储，没有运算承诺）");

    // 布局契约。断言必须放在类型完整之后：类体内对自身求 sizeof / is_trivially_copyable 都是
    // 非法表达式（不完整类型），所以契约钉在类外的公开别名上。
    static_assert(sizeof(vec2) == 16, "SIMD 的 vec2 必须是 16 字节（8 字节存储 + 8 字节 ABI 尾部填充）");
    static_assert(sizeof(vec3) == 16, "SIMD 的 vec3 必须是 16 字节（4 个 float 槽位，含 1 个 padding lane）");
    static_assert(sizeof(vec4) == 16, "SIMD 的 vec4 必须是 16 字节（4 个 float 槽位，无 padding）");
    static_assert(alignof(vec2) == kVectorAlignmentBytes, "SIMD 的 vec2 必须 16 字节对齐（D0-02）");
    static_assert(alignof(vec3) == kVectorAlignmentBytes, "SIMD 的 vec3 必须 16 字节对齐（D0-02）");
    static_assert(alignof(vec4) == kVectorAlignmentBytes, "SIMD 的 vec4 必须 16 字节对齐（D0-02）");
    static_assert(std::is_trivially_copyable_v<vec2> && std::is_trivially_copyable_v<vec3>
                      && std::is_trivially_copyable_v<vec4>,
                  "vector 必须可平凡复制：按值跨模块传递不得依赖任何非平凡语义");
    static_assert(std::is_standard_layout_v<vec2> && std::is_standard_layout_v<vec3>
                      && std::is_standard_layout_v<vec4>,
                  "vector 必须是 standard layout：布局契约不接受编译器自行插入的成员");
    static_assert(std::is_nothrow_default_constructible_v<vec2> && std::is_nothrow_default_constructible_v<vec3>
                      && std::is_nothrow_default_constructible_v<vec4>,
                  "vector 的默认构造必须 noexcept");
} // namespace ID::Math::SIMD
