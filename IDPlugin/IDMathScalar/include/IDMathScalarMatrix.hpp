#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "Common/Core.hpp"
#include "Kernel/CoreTypes.hpp"

/**
 *  ID.Math.Scalar 的 `matrix<Rows, Cols>`：存储层与访问面（不含运算）
 *  1. 唯一数据载体是紧凑的 `float storage[kStorageCount]`（`kStorageCount == Rows × Cols`）：
 *     Scalar backend 的 `ColumnStride == Rows`，**列 padding 恒为 0 字节**；
 *     `data_len() == Rows × Cols`，`storage_bytes() == sizeof(matrix)`；
 *  2. 存储约定是两条独立规则：**列主序**（元素偏移 `offset(r, c) == c × ColumnStride + r`）
 *     与**列向量**（矩阵右乘列向量，见运算头）。两者不得混为一谈：列主序只规定元素排列，
 *     不规定向量乘的方向；
 *  3. 存储层对任意正的 `Rows` / `Cols`（不超实现上限）可用；`Rows, Cols ∈ {2,3,4}` 的运算收窄
 *     属于运算头，本头不提供任何运算；
 *  4. 默认构造零初始化（D0-03）；默认构造得到的是零矩阵，单位矩阵必须显式调用运算头的
 *     `identity<N>()`，两者不得混同；
 *  5. 访问面有两条路：`get_element(row, col)` 返回 `float&` / `const float&`，是**性能路径**；
 *     `m[row][col]` 走轻量行代理（`RowProxy` / `ConstRowProxy`），不拷贝整行、不分配内存，
 *     但**不承诺零开销**（D0-04），也**不得**当作 row-contiguous span 使用——代理没有
 *     `data()` / `size()`，也不保证行内元素连续（列主序下同一行的元素相隔一个 stride）；
 *     右值矩阵的 `operator[]` 被 `= delete`（代理保存引用，临时矩阵在完整表达式结束时销毁）；
 *  6. 布局契约（`sizeof` / `alignof` / `ColumnStride` / `trivially copyable` / `standard layout`）
 *     由本头末尾的 `static_assert` 钉住；调用方不得靠 `sizeof` 猜布局，也不得假设与 SIMD backend
 *     的同名类型同布局，更不得跨 backend `reinterpret_cast`；
 *  7. 序列化不得直接 `memcpy` 内部布局（列顺序与 stride 都不是交换格式）：必须逐元素读写。
 */

namespace ID::Math::Scalar
{
    namespace detail
    {
        /// 单个维度（行数或列数）上限：实现资源上限，不是占位符
        inline constexpr size_t kMaxMatrixExtent = 256;

        /// 矩阵形状合法：两个维度都为正，且都不超实现上限
        template <size_t Rows, size_t Cols>
        inline constexpr bool is_valid_matrix_shape_v =
            (Rows > 0) && (Cols > 0) && (Rows <= kMaxMatrixExtent) && (Cols <= kMaxMatrixExtent);

        /**
         *  @brief `Rows × Cols` 个 `float` 的总字节数不溢出 `size_t`
         *  @tparam Rows 逻辑行数
         *  @tparam Cols 逻辑列数
         *  @note `Cols > 0` 的短路条件写在最前面：形状非法时根本不做除法，因此 `matrix<2, 0>`
         *        这类误用得到的是"约束不满足"的常规诊断，而不是常数表达式里的除零硬错误。
         *        在上限之内本条件不可能失败，但"乘法溢出必须在编译期拒绝"需要一条可执行判据：
         *        上限若被调高，本条件就是唯一的溢出防线。
         */
        template <size_t Rows, size_t Cols>
        inline constexpr bool is_within_matrix_extent_bytes_v =
            (Cols > 0) && (Rows <= ((SIZE_MAX / sizeof(float)) / Cols));
    } // namespace detail

    /**
     *  @brief 定长稠密矩阵：元素类型固定 `float`，列主序 + 列向量约定
     *  @tparam Rows 逻辑行数：大于 0、不超实现上限
     *  @tparam Cols 逻辑列数：大于 0、不超实现上限，且 `Rows × Cols` 个 `float` 不溢出
     *  @note 元素类型固定 `float` 是 v1 的冻结口径：本 backend 不提供模板化元素类型的矩阵，
     *        也没有 `double` 重载或隐式提升路径。
     *        默认构造零初始化（这是契约，不是"零成本"结论）；拷贝/赋值是隐式生成的成员语义，
     *        因此对象保持可平凡复制；对象内没有第二份可写状态。
     */
    template <size_t Rows, size_t Cols>
        requires detail::is_valid_matrix_shape_v<Rows, Cols> && detail::is_within_matrix_extent_bytes_v<Rows, Cols>
    class matrix
    {
    public:
        /// 逻辑行数（= 模板参数 `Rows`）
        static inline constexpr size_t kRowCount = Rows;

        /// 逻辑列数（= 模板参数 `Cols`）
        static inline constexpr size_t kColumnCount = Cols;

        /// 列 stride：同一列内相邻两行元素的间隔；Scalar backend 恒等于行数（没有列 padding）
        static inline constexpr size_t kColumnStride = Rows;

        /// 物理存储的 `float` 个数：`kColumnStride × kColumnCount`（Scalar 恒为 `Rows × Cols`）
        static inline constexpr size_t kStorageCount = kColumnStride * kColumnCount;

        /**
         *  @brief 默认构造：全部元素零初始化
         *  @note 零初始化是公开契约（D0-03）；`uninitialized` tag 不在本版接口内，
         *        也不得把本构造的代价描述成"零开销"。
         *        零矩阵不是单位矩阵：单位矩阵必须显式调用 `identity<N>()`。
         */
        ID_FORCEINLINE constexpr matrix() noexcept
            : storage{}
        {
        }

        /**
         *  @brief 元素访问（热路径，可写）
         *  @param row 行下标；**前提** `row < Rows`，越界即未定义行为
         *  @param col 列下标；**前提** `col < Cols`，越界即未定义行为
         *  @return 该元素的引用，地址即 `data() + col × column_stride() + row`
         *  @note 性能路径统一使用本入口（D0-04）。本入口不做检查、不做钳制、不返回错误码。
         *        返回的是引用，因此对临时矩阵调用本入口会把引用悬到已销毁对象上——与 `data()`
         *        的寿命规则相同，属调用方违约。
         */
        ID_FORCEINLINE constexpr float& get_element(size_t row, size_t col) noexcept
        {
            return storage[col * kColumnStride + row];
        }

        /// 元素访问（只读）；前提与可写版本相同
        ID_FORCEINLINE constexpr const float& get_element(size_t row, size_t col) const noexcept
        {
            return storage[col * kColumnStride + row];
        }

        /**
         *  @brief 首元素地址（可写）
         *  @return 指向 `storage[0]` 的指针；其后连续 `data_len()` 个 `float`，按**列主序**排列
         *          （第 0 列的 `Rows` 个元素在前，第 1 列紧随其后）
         *  @note 可读写长度由 `data_len()` 给出，不要用 `sizeof(matrix) / sizeof(float)` 之外的
         *        任何猜测代替它；SIMD backend 的列可能带 padding，因此两个 backend 的指针
         *        不得互相重解释。该布局不是交换格式：跨进程/跨 backend 传递必须逐元素处理。
         */
        ID_FORCEINLINE constexpr float* data() noexcept
        {
            return storage;
        }

        /// 首元素地址（只读）；长度与排列约定同可写版本
        ID_FORCEINLINE constexpr const float* data() const noexcept
        {
            return storage;
        }

        /// 经 `data()` 可读写的元素个数：`Cols × ColumnStride`（Scalar 恒为 `Rows × Cols`）
        ID_FORCEINLINE constexpr size_t data_len() const noexcept
        {
            return kStorageCount;
        }

        /// 存储字节数：`kStorageCount × sizeof(float)`，恒等于 `sizeof(matrix)`（没有列 padding）
        ID_FORCEINLINE constexpr size_t storage_bytes() const noexcept
        {
            return sizeof(storage);
        }

        /// 列 stride：`get_element(r, c)` 与 `get_element(r, c + 1)` 的地址差
        ID_FORCEINLINE constexpr size_t column_stride() const noexcept
        {
            return kColumnStride;
        }

        /// 逻辑行数
        ID_FORCEINLINE constexpr size_t rows() const noexcept
        {
            return kRowCount;
        }

        /// 逻辑列数
        ID_FORCEINLINE constexpr size_t cols() const noexcept
        {
            return kColumnCount;
        }

        /**
         *  @brief 行代理（非 const）：`m[row]` 的返回类型，配合 `[col]` 构成 `m[row][col]`
         *  @note 代理只保存"矩阵引用 + 行下标"两样东西：**不拷贝整行、不分配内存、没有虚表**。
         *        它不是 row-contiguous span：没有 `data()` / `size()`，且列主序下同一行的元素
         *        并不连续，因此不得按连续区间遍历或把它传给任何 span 形参。
         *        代理不承诺零开销（D0-04）：需要确定的访问代价时用 `get_element(row, col)`。
         */
        class RowProxy
        {
        public:
            /**
             *  @brief 本行第 `col` 列的元素引用
             *  @param col 列下标；**前提** `col < Cols`，越界即未定义行为
             *  @return 该元素的引用
             *  @note 代理保存矩阵引用，因此其寿命不得超过矩阵本身。
             */
            ID_FORCEINLINE constexpr float& operator[](size_t col) const noexcept
            {
                return m_target.get_element(m_row, col);
            }

        private:
            friend class matrix;    // 只有矩阵自己能造代理，代理不是对外构造入口

            ID_FORCEINLINE constexpr RowProxy(matrix& target, size_t row) noexcept
                : m_target(target)
                , m_row(row)
            {
            }

            matrix& m_target;
            size_t  m_row;
        };

        /**
         *  @brief 行代理（const）：`const matrix` 的 `m[row]` 返回本类型，元素只读
         *  @note 与 `RowProxy` 的约定一致：不拷贝整行、不分配、不承诺零开销、不是 span。
         */
        class ConstRowProxy
        {
        public:
            /**
             *  @brief 本行第 `col` 列的元素引用（只读）
             *  @param col 列下标；**前提** `col < Cols`，越界即未定义行为
             *  @return 该元素的常量引用
             */
            ID_FORCEINLINE constexpr const float& operator[](size_t col) const noexcept
            {
                return m_target.get_element(m_row, col);
            }

        private:
            friend class matrix;

            ID_FORCEINLINE constexpr ConstRowProxy(const matrix& target, size_t row) noexcept
                : m_target(target)
                , m_row(row)
            {
            }

            const matrix& m_target;
            size_t        m_row;
        };

        /**
         *  @brief 行访问：`m[row][col]` 的入口（非 const）
         *  @param row 行下标；**前提** `row < Rows`，越界即未定义行为
         *  @return 行代理，保存 `*this` 的引用与 `row`
         *  @note 只在左值矩阵上可用：右值的同名重载被 `= delete`，因为代理保存的引用会立刻悬空。
         */
        ID_FORCEINLINE constexpr RowProxy operator[](size_t row) & noexcept
        {
            return RowProxy(*this, row);
        }

        /// 行访问（const 左值矩阵）：返回只读代理
        ID_FORCEINLINE constexpr ConstRowProxy operator[](size_t row) const& noexcept
        {
            return ConstRowProxy(*this, row);
        }

        /// 右值矩阵禁止下标：代理保存引用，临时矩阵在完整表达式结束后销毁，代理必然悬空（D0-04）
        RowProxy operator[](size_t row) && = delete;

        /// const 右值矩阵同样禁止下标：const 右值临时矩阵的代理一样会悬空
        ConstRowProxy operator[](size_t row) const&& = delete;

    private:
        // 唯一数据载体：列主序，每列 kColumnStride 个槽位（Scalar 无列 padding）
        float storage[kStorageCount];
    };

    namespace detail
    {
        /**
         *  @brief Scalar 矩阵的布局契约自证
         *  @tparam Rows 逻辑行数
         *  @tparam Cols 逻辑列数
         *  @note 逐项覆盖：`ColumnStride == Rows`（无列 padding）、`sizeof` 恰为 `Rows × Cols` 个
         *        `float`、`sizeof` 与 `ColumnStride` 的关系成立、对齐等于元素对齐、可平凡复制、
         *        standard layout。任一项不成立，下面 9 种 v1 形状的断言就会失败。
         */
        template <size_t Rows, size_t Cols>
        inline constexpr bool is_v1_matrix_layout_contract_v =
            (matrix<Rows, Cols>::kColumnStride == Rows)
            && (matrix<Rows, Cols>::kStorageCount == Rows * Cols)
            && ((matrix<Rows, Cols>::kColumnStride * matrix<Rows, Cols>::kColumnCount * sizeof(float))
                == sizeof(matrix<Rows, Cols>))
            && (sizeof(matrix<Rows, Cols>) == Rows * Cols * sizeof(float))
            && (alignof(matrix<Rows, Cols>) == alignof(float))
            && std::is_trivially_copyable_v<matrix<Rows, Cols>>
            && std::is_standard_layout_v<matrix<Rows, Cols>>;
    } // namespace detail

    // v1 的 9 种形状：sizeof 必须恰为 Rows × Cols × sizeof(float)（紧凑存储，无列 padding）。
    // 断言放在类型完整之后：类体内对自身求 sizeof 是非法表达式。
    static_assert(sizeof(matrix<2, 2>) == 16, "Scalar 的 matrix<2,2> 必须是 16 字节（4 × float，无列 padding）");
    static_assert(sizeof(matrix<2, 3>) == 24, "Scalar 的 matrix<2,3> 必须是 24 字节（6 × float，无列 padding）");
    static_assert(sizeof(matrix<2, 4>) == 32, "Scalar 的 matrix<2,4> 必须是 32 字节（8 × float，无列 padding）");
    static_assert(sizeof(matrix<3, 2>) == 24, "Scalar 的 matrix<3,2> 必须是 24 字节（6 × float，无列 padding）");
    static_assert(sizeof(matrix<3, 3>) == 36, "Scalar 的 matrix<3,3> 必须是 36 字节（9 × float，无列 padding）");
    static_assert(sizeof(matrix<3, 4>) == 48, "Scalar 的 matrix<3,4> 必须是 48 字节（12 × float，无列 padding）");
    static_assert(sizeof(matrix<4, 2>) == 32, "Scalar 的 matrix<4,2> 必须是 32 字节（8 × float，无列 padding）");
    static_assert(sizeof(matrix<4, 3>) == 48, "Scalar 的 matrix<4,3> 必须是 48 字节（12 × float，无列 padding）");
    static_assert(sizeof(matrix<4, 4>) == 64, "Scalar 的 matrix<4,4> 必须是 64 字节（16 × float，无列 padding）");

    static_assert((detail::is_v1_matrix_layout_contract_v<2, 2> && detail::is_v1_matrix_layout_contract_v<2, 3>
                   && detail::is_v1_matrix_layout_contract_v<2, 4> && detail::is_v1_matrix_layout_contract_v<3, 2>
                   && detail::is_v1_matrix_layout_contract_v<3, 3> && detail::is_v1_matrix_layout_contract_v<3, 4>
                   && detail::is_v1_matrix_layout_contract_v<4, 2> && detail::is_v1_matrix_layout_contract_v<4, 3>
                   && detail::is_v1_matrix_layout_contract_v<4, 4>),
                  "Scalar 的 9 种 v1 形状必须满足：ColumnStride == Rows、sizeof == Rows × Cols × sizeof(float)、"
                  "alignof == alignof(float)、可平凡复制且是 standard layout");

    // 行代理的布局自证：代理只保存"矩阵引用 + 行下标"，因此 `m[row]` 不会复制整行、不会分配内存、
    // 也不会引入自定义拷贝/析构语义。这**不是**零开销承诺（D0-04），只是"没有整行拷贝"的证据。
    static_assert(sizeof(matrix<3, 3>::RowProxy) == sizeof(void*) + sizeof(size_t),
                  "行代理必须只保存一个矩阵引用与一个行下标（不得携带整行副本）");
    static_assert(sizeof(matrix<3, 3>::ConstRowProxy) == sizeof(const void*) + sizeof(size_t),
                  "const 行代理同样只能保存一个矩阵引用与一个行下标");
    static_assert(std::is_trivially_copyable_v<matrix<3, 3>::RowProxy>
                      && std::is_trivially_copyable_v<matrix<3, 3>::ConstRowProxy>,
                  "行代理必须可平凡复制：不得引入自定义拷贝/析构语义（也就不可能有堆分配）");
} // namespace ID::Math::Scalar
