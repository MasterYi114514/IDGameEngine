#pragma once

#include <cstddef>
#include <type_traits>

#include "Common/Core.hpp"
#include "Kernel/CoreTypes.hpp"

#include "IDMathSIMDVector.hpp"
#include "detail/IDMathSIMDDetailFloatBits.hpp"
#include "detail/IDMathSIMDDetailSse2.hpp"
#include "detail/IDMathSIMDDetailTraits.hpp"

/**
 *  ID.Math.SIMD 的 `matrix<Rows, Cols>`：存储、访问与运算（元素固定 `float`）
 *  1. 逻辑形状是 `Rows × Cols`，`Rows` / `Cols` 只能取 `2 / 3 / 4`（v1 数学维度，计划书 §6.1），
 *     共 9 种形状（含矩形）；**列主序 + 列向量**是两条独立规则：元素偏移恒为
 *     `offset(r, c) = c * ColumnStride + r`，`matrix * vector` 按列向量的线性组合解释；
 *  2. 唯一数据载体是 `float storage[Cols * ColumnStride]`（列主序定长元素块），整个类型
 *     `alignas(16)`。`ColumnStride` 在本 backend 固定为 **4 个 float 槽位 = 16 字节**（D0-02 允许
 *     `ColumnStride > Rows`，本头把该取值冻结为常量并用 `static_assert` 钉住）：
 *     - `Rows == 2`：每列 2 个逻辑元素 + **2 个 padding 槽位（8 字节）**；
 *     - `Rows == 3`：每列 3 个逻辑元素 + **1 个 padding 槽位（4 字节）**；
 *     - `Rows == 4`：无 padding（`ColumnStride == Rows`）。
 *     于是 `sizeof(matrix) == Cols * 16`，恒为 16 的整数倍，**没有** ABI 尾部填充；
 *     每列起点都 16 字节对齐，列载入用对齐 load，读取的 16 字节始终落在 `storage` 内（不越界）；
 *  3. **padding 是公开布局的一部分**：列尾 `ColumnStride - Rows` 个槽位**不属于公开值**——
 *     `data()` 的可寻址范围是 `Cols * ColumnStride` 个 `float`（长度 = `storage_bytes()/sizeof(float)`），
 *     但只有 `offset(r, c)`（`r < Rows`）承载可读的逻辑元素，padding 槽位**不可读**；
 *     构造入口与所有返回新矩阵的运算都把 padding 槽位写成 `+0.0f`（位模式全零），`try_inverse` /
 *     `unpack` 这类写入调用方对象的入口也一样；从 padding 槽位读值属于契约外用法；
 *  4. **列主序不是紧凑数组**：`data()` 不能当 row-contiguous `span` 用，也不能当"紧凑 Rows × Cols
 *     数组"遍历；跨 backend 转换与外部序列化只能显式逐元素，或走 `pack` / `unpack`（本头提供），
 *     **禁止** `reinterpret_cast` 与直接 `memcpy` 内部布局；
 *  5. 访问面：性能路径统一用 `get_element(row, col)`（返回 `float&` / `const float&`）；`m[row][col]`
 *     的 `RowProxy` / `ConstRowProxy` **不承诺零开销**（D0-04），只保证无整行拷贝、无堆分配；
 *     右值矩阵的 `operator[]` 被 `= delete`（避免悬空代理）；
 *  6. 运算：`matrix<R,K> * matrix<K,C>`、`matrix<R,C> * vector<float,C>`、`transpose`、
 *     `identity`、`determinant`、`try_inverse`（仅方阵 2/3/4）。`identity` 必须显式调用，默认构造
 *     是零矩阵（D0-03），两者不得混同；**不**提供逐元素乘的 `operator*(matrix, matrix)`；
 *  7. 精度与失败语义：`matrix * matrix` / `matrix * vector` 用 128-bit lane 的列式 SIMD 累加
 *     （每个输出列 = `Σ_k A(k) * B(k,c)`），归一化与求逆的中间量全部是 `float`，不提升到 `double`；
 *     `try_inverse` 以 finite input 为契约前置，非有限输入用位模式前检判出后返回 `false`，
 *     近奇异判定与 scale 相关（见 `kNearSingularRelativeThreshold`），**失败时输出参数不变**；
 *  8. intrinsic 值只作临时寄存器与计算中间值：矩阵对象里只有 `storage` 一份可写状态；
 *     本头没有 `union` punning，也没有 strict-aliasing 违规的 `reinterpret_cast`；
 *  9. `constexpr`：纯标量闭包入口（访问面、`identity`、`determinant`）保持 `constexpr`；
 *     含 intrinsic 的入口（矩阵乘、`transpose` 的 4×4 路径、`try_inverse`）**不是** `constexpr`
 *     （C++20 下 intrinsic 不是常量表达式），这不是语义差异，只影响能否在编译期求值。
 */

namespace ID::Math::SIMD
{
    /// 矩阵每列的物理槽位数（= 4 个 `float` 槽位 = 16 字节）；D0-02 允许的列 padding 的冻结取值
    inline constexpr size_t kMatrixColumnStride = 4;

    /// 矩阵的对齐字节数：128-bit lane，保证每列起点 16 字节对齐
    inline constexpr size_t kMatrixAlignmentBytes = 16;

    /**
     *  `try_inverse` 的近奇异相对阈值
     *
     *  判定式：`|det(A)| <= kNearSingularRelativeThreshold * scale^Rows`，其中
     *  `scale = max_r Σ_c |A(r,c)|`（∞-范数）。它是**与 scale 相关**的判据：把矩阵整体乘 `s`
     *  会使 `det` 乘 `s^Rows`、阈值同样乘 `s^Rows`，因此"是否被判为近奇异"不随数值缩放漂移。
     *  取值 `64 * ε`（≈ `7.63e-6`）是与实现绑定的**分类常量**，不是精度或条件数承诺：
     *  条件数远超 `1/阈值` 的矩阵即便被接受，`float` 求逆结果也没有有效位数。
     */
    inline constexpr float kNearSingularRelativeThreshold = 64.0f * detail::kFloatEpsilon;

    /**
     *  @brief 定长小矩阵（列主序 + 列向量）
     *  @tparam Rows 逻辑行数：`2` / `3` / `4`
     *  @tparam Cols 逻辑列数：`2` / `3` / `4`
     *
     *  @note 布局与 padding（公开契约）：
     *        - 唯一成员是 `float storage[kStorageCount]`，`kStorageCount == Cols * ColumnStride`；
     *        - 每列占 `ColumnStride`（= 4）个槽位，元素位于 `offset(r, c) = c * ColumnStride + r`；
     *        - 列尾 `kPaddingSlotsPerColumn`（= `ColumnStride - Rows`，即 2 / 1 / 0）个槽位是
     *          padding：字节数为 `kColumnPaddingBytes`（8 / 4 / 0），整个矩阵共
     *          `kTotalPaddingBytes` 字节 padding；padding 不承载公开值、不可读、不参与任何运算；
     *        - 默认构造零初始化（D0-03，含 padding），与 `identity()` 是两件不同的事。
     *  @note 类型层面：`trivially copyable` + `standard layout`，`noexcept`，无分配，拷贝/赋值是隐式
     *        生成的成员语义（整块存储复制，包含 padding 槽位的零值）。
     */
    template <size_t Rows, size_t Cols>
        requires detail::is_valid_matrix_extent_v<Rows> && detail::is_valid_matrix_extent_v<Cols>
    class alignas(kMatrixAlignmentBytes) matrix
    {
    public:
        /// 逻辑行数（= 模板参数 `Rows`）
        static inline constexpr size_t kRowCount = Rows;

        /// 逻辑列数（= 模板参数 `Cols`）
        static inline constexpr size_t kColumnCount = Cols;

        /// 每列的物理槽位数（= 4 个 `float` 槽位 = 16 字节）
        static inline constexpr size_t kColumnStride = kMatrixColumnStride;

        /// 存储槽位总数：`Cols * kColumnStride`
        static inline constexpr size_t kStorageCount = Cols * kColumnStride;

        /// 每列尾部 padding 槽位数：`kColumnStride - Rows`（Rows = 2 / 3 / 4 时为 2 / 1 / 0）
        static inline constexpr size_t kPaddingSlotsPerColumn = kColumnStride - Rows;

        /// 每列 padding 字节数：`kPaddingSlotsPerColumn * sizeof(float)`（8 / 4 / 0）
        static inline constexpr size_t kColumnPaddingBytes = kPaddingSlotsPerColumn * sizeof(float);

        /// 整个矩阵的 padding 字节数：`Cols * kColumnPaddingBytes`
        static inline constexpr size_t kTotalPaddingBytes = Cols * kColumnPaddingBytes;

        // 列 stride 必须装得下逻辑行数：否则"列"本身失效，下面的偏移公式不再成立。
        static_assert(Rows <= kColumnStride, "矩阵行数不得超过列 stride（每列固定 4 个 float 槽位）");

        /**
         *  @brief 默认构造：全部槽位零初始化（零矩阵，**不是**单位矩阵）
         *  @note 零初始化是公开契约（D0-03），包含列 padding 槽位；`identity()` 必须显式调用。
         */
        ID_FORCEINLINE constexpr matrix() noexcept
            : storage{}
        {
        }

        /**
         *  @brief 单位矩阵（必须显式调用）
         *  @return 对角为 `1.0f`、其余逻辑元素与全部 padding 槽位为 `0.0f` 的方阵
         *  @note 只对方阵存在（`Rows == Cols`）；不得用默认构造表达单位矩阵（D0-03）。
         */
        ID_FORCEINLINE static constexpr matrix identity() noexcept
            requires (Rows == Cols)
        {
            matrix result;
            for (size_t i = 0; i < Rows; ++i)
            {
                result.storage[i * kColumnStride + i] = 1.0f;
            }
            return result;
        }

        /**
         *  @brief 存储块首槽位地址（可写）
         *  @return 指向 `storage[0]` 的指针，16 字节对齐
         *  @note **可寻址长度**是 `Cols * ColumnStride` 个 `float`（= `storage_bytes()/sizeof(float)`），
         *        但只有 `offset(r, c)`（`r < Rows`）承载可读的逻辑元素；每列尾部的
         *        `ColumnStride - Rows` 个 padding 槽位**不可读**。它不是紧凑的 `Rows × Cols` 数组，
         *        **不得**当 row-contiguous `span` 使用，也不得用它做跨 backend 的重解释。
         */
        ID_FORCEINLINE constexpr float* data() noexcept
        {
            return storage;
        }

        /// 存储块首槽位地址（只读）；长度与 padding 约束同非 const 版本
        ID_FORCEINLINE constexpr const float* data() const noexcept
        {
            return storage;
        }

        /// 存储载体字节数：`Cols * ColumnStride * sizeof(float)`（恒等于 `sizeof(matrix)`，无 ABI 尾部填充）
        ID_FORCEINLINE constexpr size_t storage_bytes() const noexcept
        {
            return sizeof(storage);
        }

        /// 每列的物理槽位数（= `kColumnStride`，稳定值 4）
        ID_FORCEINLINE constexpr size_t column_stride() const noexcept
        {
            return kColumnStride;
        }

        /// 逻辑行数（= `Rows`）
        ID_FORCEINLINE constexpr size_t rows() const noexcept
        {
            return kRowCount;
        }

        /// 逻辑列数（= `Cols`）
        ID_FORCEINLINE constexpr size_t cols() const noexcept
        {
            return kColumnCount;
        }

        /**
         *  @brief 元素访问（**性能路径**，D0-04）
         *  @param row 行下标；**前提** `row < Rows`，越界即未定义行为
         *  @param col 列下标；**前提** `col < Cols`，越界即未定义行为
         *  @return 该元素引用；偏移为 `col * ColumnStride + row`
         *  @note 本入口不做检查、不钳制、不返回错误码；拿 `row >= Rows` 去读列 padding 同样是
         *        契约外行为。`m[row][col]` 的代理只是它的语法糖，性能路径统一用本函数。
         */
        ID_FORCEINLINE constexpr float& get_element(size_t row, size_t col) noexcept
        {
            return storage[col * kColumnStride + row];
        }

        /// 只读元素访问；前提与 `get_element` 的非 const 版本相同
        ID_FORCEINLINE constexpr const float& get_element(size_t row, size_t col) const noexcept
        {
            return storage[col * kColumnStride + row];
        }

        /**
         *  @brief 行代理（`m[row]` 的第一层，非 const 矩阵）
         *  @note 代理只持有"矩阵指针 + 行号"，**无整行拷贝、无堆分配**（D0-04）；它不是
         *        row-contiguous `span`，不能当连续内存遍历。代理**不承诺零开销**：它只是
         *        `get_element(row, col)` 的语法糖，热路径请直接用 `get_element`。
         */
        class RowProxy
        {
        public:
            /**
             *  @brief 构造行代理
             *  @param owner 目标矩阵；**前提** 生命周期长于本代理
             *  @param row 行下标；**前提** `row < Rows`
             */
            ID_FORCEINLINE constexpr RowProxy(matrix& owner, size_t row) noexcept
                : m_owner(&owner)
                , m_row(row)
            {
            }

            /// 取该行第 `col` 列元素；**前提** `col < Cols`
            ID_FORCEINLINE constexpr float& operator[](size_t col) const noexcept
            {
                return m_owner->get_element(m_row, col);
            }

        private:
            matrix* m_owner;    // 代理不拥有矩阵：生命周期由调用方保证
            size_t  m_row;      // 目标行号
        };

        /// 行代理（`m[row]` 的第一层，const 矩阵）；约束与 `RowProxy` 相同
        class ConstRowProxy
        {
        public:
            /// 构造只读行代理；前提与 `RowProxy` 相同
            ID_FORCEINLINE constexpr ConstRowProxy(const matrix& owner, size_t row) noexcept
                : m_owner(&owner)
                , m_row(row)
            {
            }

            /// 取该行第 `col` 列元素（只读）；**前提** `col < Cols`
            ID_FORCEINLINE constexpr const float& operator[](size_t col) const noexcept
            {
                return m_owner->get_element(m_row, col);
            }

        private:
            const matrix* m_owner;  // 代理不拥有矩阵：生命周期由调用方保证
            size_t        m_row;    // 目标行号
        };

        /// 取行代理；只对可寻址的左值矩阵存在（右值被 `= delete`）
        ID_FORCEINLINE constexpr RowProxy operator[](size_t row) & noexcept
        {
            return RowProxy(*this, row);
        }

        /// 取只读行代理；只对左值矩阵存在
        ID_FORCEINLINE constexpr ConstRowProxy operator[](size_t row) const& noexcept
        {
            return ConstRowProxy(*this, row);
        }

        // 右值矩阵的代理会立刻悬空（临时对象在完整表达式后销毁），因此显式拒绝。
        RowProxy operator[](size_t row) && = delete;
        ConstRowProxy operator[](size_t row) const&& = delete;

    private:
        // 唯一数据载体：列主序定长元素块，每列 kColumnStride 个槽位。
        // 类型整体 alignas(16)：storage[0] 因此 16 字节对齐，每列起点也 16 字节对齐，
        // 列载入不需要额外对齐分支。
        float storage[kStorageCount];
    };

    // 布局契约（D0-02）：列 stride、每列 padding、总 padding 与 9 种形状的实际字节数。
    // 断言放在类型完整之后：类体内对自身求 sizeof 是不完整类型表达式。
    static_assert(matrix<2, 2>::kColumnStride == kMatrixColumnStride && matrix<3, 3>::kColumnStride == kMatrixColumnStride
                      && matrix<4, 4>::kColumnStride == kMatrixColumnStride,
                  "SIMD 矩阵的列 stride 冻结为 4 个 float 槽位（16 字节）");
    static_assert(matrix<2, 2>::kPaddingSlotsPerColumn == 2 && matrix<2, 2>::kColumnPaddingBytes == 8
                      && matrix<2, 2>::kTotalPaddingBytes == 16,
                  "matrix<2, Cols> 每列 2 个 padding 槽位（8 字节）");
    static_assert(matrix<3, 3>::kPaddingSlotsPerColumn == 1 && matrix<3, 3>::kColumnPaddingBytes == 4
                      && matrix<3, 3>::kTotalPaddingBytes == 12,
                  "matrix<3, Cols> 每列 1 个 padding 槽位（4 字节）");
    static_assert(matrix<4, 4>::kPaddingSlotsPerColumn == 0 && matrix<4, 4>::kTotalPaddingBytes == 0,
                  "matrix<4, Cols> 没有列 padding：ColumnStride == Rows");
    static_assert(sizeof(matrix<2, 2>) == 32 && sizeof(matrix<2, 3>) == 48 && sizeof(matrix<2, 4>) == 64,
                  "matrix<2, Cols> 必须是 Cols × 16 字节（无 ABI 尾部填充）");
    static_assert(sizeof(matrix<3, 2>) == 32 && sizeof(matrix<3, 3>) == 48 && sizeof(matrix<3, 4>) == 64,
                  "matrix<3, Cols> 必须是 Cols × 16 字节（无 ABI 尾部填充）");
    static_assert(sizeof(matrix<4, 2>) == 32 && sizeof(matrix<4, 3>) == 48 && sizeof(matrix<4, 4>) == 64,
                  "matrix<4, Cols> 必须是 Cols × 16 字节（无 ABI 尾部填充）");
    static_assert(alignof(matrix<2, 2>) == kMatrixAlignmentBytes && alignof(matrix<3, 3>) == kMatrixAlignmentBytes
                      && alignof(matrix<4, 4>) == kMatrixAlignmentBytes,
                  "SIMD 矩阵必须 16 字节对齐（每列起点 16 字节对齐，列载入用对齐 load）");
    static_assert(std::is_trivially_copyable_v<matrix<3, 4>> && std::is_trivially_copyable_v<matrix<4, 4>>,
                  "matrix 必须可平凡复制：按值跨模块传递不得依赖任何非平凡语义");
    static_assert(std::is_standard_layout_v<matrix<2, 3>> && std::is_standard_layout_v<matrix<4, 4>>,
                  "matrix 必须是 standard layout：布局契约不接受编译器自行插入的成员");
    static_assert(std::is_nothrow_default_constructible_v<matrix<2, 4>>,
                  "matrix 的默认构造必须 noexcept");

    namespace detail
    {
        /**
         *  @brief 小方阵行列式：局部副本 + 部分主元高斯消元
         *  @tparam Rows 阶数：`2` / `3` / `4`
         *  @param m 源矩阵；只读
         *  @return 行列式；某一列在消元后主元为 `0`（奇异）时返回 `0.0f`
         *  @note 选消元而不是按行余子式展开：部分主元把乘数限制在 `|l| <= 1`，误差增长比直接展开小；
         *        中间量全是 `float`，不提升到 `double`（D0-05 禁止隐式提升）。局部副本只是计算
         *        中间量，不是第二份持久状态：矩阵对象里仍然只有 `storage`。
         */
        template <size_t Rows>
        constexpr float determinant_small(const matrix<Rows, Rows>& m) noexcept
        {
            float a[Rows * Rows] = {};
            for (size_t c = 0; c < Rows; ++c)
            {
                for (size_t r = 0; r < Rows; ++r)
                {
                    a[c * Rows + r] = m.get_element(r, c);
                }
            }

            float determinant_value = 1.0f;
            for (size_t c = 0; c < Rows; ++c)
            {
                // 部分主元：在本列 c..Rows-1 里挑绝对值最大的行，避免除以过小的数。
                size_t pivot = c;
                float pivot_magnitude = abs_value(a[c * Rows + c]);
                for (size_t r = c + 1; r < Rows; ++r)
                {
                    const float magnitude = abs_value(a[c * Rows + r]);
                    if (magnitude > pivot_magnitude)
                    {
                        pivot = r;
                        pivot_magnitude = magnitude;
                    }
                }
                if (pivot_magnitude == 0.0f)
                {
                    return 0.0f; // 本列（含主元）全零：奇异
                }
                if (pivot != c)
                {
                    // 行交换要作用到还没被消去的列（j >= c）；更早的列已不再被读取。
                    for (size_t j = c; j < Rows; ++j)
                    {
                        const float temporary = a[j * Rows + c];
                        a[j * Rows + c] = a[j * Rows + pivot];
                        a[j * Rows + pivot] = temporary;
                    }
                    determinant_value = -determinant_value;
                }

                const float pivot_value = a[c * Rows + c];
                determinant_value *= pivot_value;
                for (size_t r = c + 1; r < Rows; ++r)
                {
                    const float factor = a[c * Rows + r] / pivot_value;
                    if (factor == 0.0f)
                    {
                        continue;
                    }
                    for (size_t j = c + 1; j < Rows; ++j)
                    {
                        a[j * Rows + r] -= factor * a[j * Rows + c];
                    }
                }
            }
            return determinant_value;
        }

        /**
         *  @brief 矩阵的 ∞-范数：各行"元素绝对值之和"的最大值
         *  @return `max_r Σ_c |m(r,c)|`；全零矩阵返回 `0.0f`
         *  @note 用于 `try_inverse` 的**与 scale 相关**的近奇异判定：`det` 的量级随 `scale^Rows`
         *        变化，所以阈值必须乘 `scale^Rows`，不能用一个固定的绝对数。
         */
        template <size_t Rows, size_t Cols>
        ID_FORCEINLINE constexpr float max_abs_row_sum(const matrix<Rows, Cols>& m) noexcept
        {
            float maximum = 0.0f;
            for (size_t r = 0; r < Rows; ++r)
            {
                float row_sum = 0.0f;
                for (size_t c = 0; c < Cols; ++c)
                {
                    row_sum += abs_value(m.get_element(r, c));
                }
                if (row_sum > maximum)
                {
                    maximum = row_sum;
                }
            }
            return maximum;
        }

        /**
         *  @brief `base` 的 `Exponent` 次幂（小整数指数，纯乘法）
         *  @param base 底数
         *  @param exponent 指数
         *  @return `base^Exponent`；`Exponent == 0` 时返回 `1.0f`
         *  @note 不用 `std::pow`：它在 C++20 下不是 `constexpr`，且小整数幂直接相乘更可预测。
         */
        ID_FORCEINLINE constexpr float pow_natural(float base, size_t exponent) noexcept
        {
            float result = 1.0f;
            for (size_t i = 0; i < exponent; ++i)
            {
                result *= base;
            }
            return result;
        }
    } // namespace detail

    /**
     *  @brief 矩阵乘矩阵：`matrix<R,K> * matrix<K,C>` -> `matrix<R,C>`
     *  @param lhs 左矩阵（`R × K`）
     *  @param rhs 右矩阵（`K × C`）
     *  @return `R × C` 结果：第 `c` 列为 `Σ_k lhs 的第 k 列 * rhs(k, c)`
     *  @note 维度不匹配（`K` 不同）时模板推导失败——**不按维度猜乘法**。实现按列做 128-bit lane
     *        累加：每次取 `lhs` 的一列（16 字节对齐载入，`R < 4` 时列 padding 被掩码清零）乘
     *        `rhs(k, c)` 的广播值，再写回结果的一列，因此 padding 不参与运算，结果列 padding 为
     *        `+0.0f`。**不**提供逐元素乘的 `operator*(matrix, matrix)`。
     */
    template <size_t R, size_t K, size_t C>
    matrix<R, C> operator*(const matrix<R, K>& lhs, const matrix<K, C>& rhs) noexcept
    {
        matrix<R, C> result;
        for (size_t c = 0; c < C; ++c)
        {
            __m128 accumulator = _mm_setzero_ps();
            for (size_t k = 0; k < K; ++k)
            {
                const __m128 column = detail::load_column<R>(lhs.data() + k * kMatrixColumnStride);
                accumulator = _mm_add_ps(accumulator, _mm_mul_ps(column, _mm_set1_ps(rhs.get_element(k, c))));
            }
            detail::store_column<R>(result.data() + c * kMatrixColumnStride, accumulator);
        }
        return result;
    }

    /**
     *  @brief 矩阵乘向量（列向量约定）：`matrix<R,C> * vector<float,C>` -> `vector<float,R>`
     *  @param m 矩阵（`R × C`）
     *  @param v 列向量（`C` 个逻辑分量）
     *  @return `R` 个逻辑分量：`result[r] = Σ_c m(r,c) * v[c]`
     *  @note 按列做 128-bit lane 累加：`Σ_c m 的第 c 列 * v[c]`。`m` 的列 padding 与 `v` 的 padding lane
     *        （`vec3` 的 lane3）都不参与运算；结果向量的 padding lane 为 `+0.0f`。
     *        维度不匹配时模板推导失败。
     */
    template <size_t R, size_t C>
    vector<float, R> operator*(const matrix<R, C>& m, const vector<float, C>& v) noexcept
    {
        __m128 accumulator = _mm_setzero_ps();
        for (size_t c = 0; c < C; ++c)
        {
            const __m128 column = detail::load_column<R>(m.data() + c * kMatrixColumnStride);
            accumulator = _mm_add_ps(accumulator, _mm_mul_ps(column, _mm_set1_ps(v[c])));
        }

        vector<float, R> result;
        detail::store_lanes<R>(result.data(), accumulator);
        return result;
    }

    /**
     *  @brief 转置：`matrix<R,C>` -> `matrix<C,R>`
     *  @param m 源矩阵
     *  @return 转置矩阵：`result(c, r) == m(r, c)`
     *  @note 4×4 走整 lane 转置（`_mm_unpacklo_ps` / `_mm_unpackhi_ps` / `_mm_movelh_ps` / `_mm_movehl_ps`，
     *        4 次列载入 + 4 次列写回）；其余 8 种形状逐元素搬运（转置本质上是跨列 gather，
     *        `ColumnStride` 不是 1，不存在"一个 lane 装一整行"的形态）。结果矩阵的列 padding 由默认
     *        构造保持 `+0.0f`，源矩阵的 padding 从不被读取。
     */
    template <size_t R, size_t C>
    matrix<C, R> transpose(const matrix<R, C>& m) noexcept
    {
        matrix<C, R> result;
        if constexpr (R == 4 && C == 4)
        {
            const __m128 column0 = detail::load_column<4>(m.data() + 0 * kMatrixColumnStride);
            const __m128 column1 = detail::load_column<4>(m.data() + 1 * kMatrixColumnStride);
            const __m128 column2 = detail::load_column<4>(m.data() + 2 * kMatrixColumnStride);
            const __m128 column3 = detail::load_column<4>(m.data() + 3 * kMatrixColumnStride);

            const __m128 pair01_low = _mm_unpacklo_ps(column0, column1);   // (m00, m01, m10, m11)
            const __m128 pair01_high = _mm_unpackhi_ps(column0, column1);  // (m20, m21, m30, m31)
            const __m128 pair23_low = _mm_unpacklo_ps(column2, column3);   // (m02, m03, m12, m13)
            const __m128 pair23_high = _mm_unpackhi_ps(column2, column3);  // (m22, m23, m32, m33)

            detail::store_column<4>(result.data() + 0 * kMatrixColumnStride, _mm_movelh_ps(pair01_low, pair23_low));
            detail::store_column<4>(result.data() + 1 * kMatrixColumnStride, _mm_movehl_ps(pair23_low, pair01_low));
            detail::store_column<4>(result.data() + 2 * kMatrixColumnStride, _mm_movelh_ps(pair01_high, pair23_high));
            detail::store_column<4>(result.data() + 3 * kMatrixColumnStride, _mm_movehl_ps(pair23_high, pair01_high));
        }
        else
        {
            for (size_t c = 0; c < C; ++c)
            {
                for (size_t r = 0; r < R; ++r)
                {
                    result.get_element(c, r) = m.get_element(r, c);
                }
            }
        }
        return result;
    }

    /**
     *  @brief 行列式（**只**对 `matrix<R,R>` 存在）
     *  @param m 方阵
     *  @return 行列式；奇异时返回 `0.0f`
     *  @note 传矩形矩阵时模板推导失败（`determinant` 不接受"按维度猜"的用法）。实现见
     *        `detail::determinant_small`：局部副本 + 部分主元高斯消元，中间量全是 `float`。
     *        与 `try_inverse` 的近奇异判定一起看：`determinant` 本身不做任何"是否可逆"的判断。
     */
    template <size_t Rows>
    ID_FORCEINLINE constexpr float determinant(const matrix<Rows, Rows>& m) noexcept
    {
        return detail::determinant_small(m);
    }

    /**
     *  @brief 求逆的 checked 入口（**只**对 `matrix<R,R>` 存在，`R ∈ {2,3,4}`）
     *  @param m 源方阵；**前提** 各元素有限（有限输入是 checked 入口的契约前置，D0-05）
     *  @param out 输出；成功时写入 `m^-1`，**失败时保持调用前的值不变**（含其列 padding）
     *  @return 成功 `true`；以下情形 `false`：`m` 含非有限元素、被判为近奇异（判定式见
     *          `kNearSingularRelativeThreshold`）、或消元过程中主元为 0
     *  @note 非有限值分类用位模式前检（`detail::is_finite_bits`），不依赖 `isfinite`，因此不会被
     *        `-ffast-math` / `/fp:fast` 消掉；该前检**只**解决"非有限值分类"，不构成任何精度、
     *        条件数或容差结论。
     *        近奇异判据与 scale 相关：`|det| <= kNearSingularRelativeThreshold * scale^Rows`，
     *        `scale` 是 ∞-范数（最大绝对行和），因此整体缩放不改变判定结果。
     *        实现先把 `m` 拷进局部增广矩阵做带部分主元的高斯-约当消元，成功后才一次性写入局部
     *        结果矩阵并赋值给 `out`，因此 `&out == &m` 安全，且每条失败分支都在写 `out` 之前返回。
     *        不提供 unchecked 版本：本 backend 不交付"静默给出脏结果"的求逆入口。
     */
    template <size_t Rows>
    bool try_inverse(const matrix<Rows, Rows>& m, matrix<Rows, Rows>& out) noexcept
    {
        for (size_t c = 0; c < Rows; ++c)
        {
            for (size_t r = 0; r < Rows; ++r)
            {
                if (!detail::is_finite_bits(m.get_element(r, c)))
                {
                    return false;
                }
            }
        }

        const float scale = detail::max_abs_row_sum(m);
        const float determinant_value = detail::determinant_small(m);
        // `|det| > 阈值 * scale^Rows` 才继续；非有限的 det 或 scale 在这里自然落到 `false`
        // （与 `inf` / `NaN` 的比较为假），不需要额外分支。
        if (!(detail::abs_value(determinant_value) > kNearSingularRelativeThreshold * detail::pow_natural(scale, Rows)))
        {
            return false;
        }

        // 增广矩阵 [m | I]，行主序局部数组；只是计算中间量，不是第二份持久状态。
        float augmented[Rows][2 * Rows];
        for (size_t r = 0; r < Rows; ++r)
        {
            for (size_t c = 0; c < Rows; ++c)
            {
                augmented[r][c] = m.get_element(r, c);
                augmented[r][Rows + c] = (r == c) ? 1.0f : 0.0f;
            }
        }

        for (size_t c = 0; c < Rows; ++c)
        {
            size_t pivot = c;
            float pivot_magnitude = detail::abs_value(augmented[c][c]);
            for (size_t r = c + 1; r < Rows; ++r)
            {
                const float magnitude = detail::abs_value(augmented[r][c]);
                if (magnitude > pivot_magnitude)
                {
                    pivot = r;
                    pivot_magnitude = magnitude;
                }
            }
            if (pivot_magnitude == 0.0f)
            {
                // 防御性分支：前面的 scale/det 前检已排除这一情形；仍然不写 out。
                return false;
            }
            if (pivot != c)
            {
                for (size_t j = 0; j < 2 * Rows; ++j)
                {
                    const float temporary = augmented[c][j];
                    augmented[c][j] = augmented[pivot][j];
                    augmented[pivot][j] = temporary;
                }
            }

            const float inverse_pivot = 1.0f / augmented[c][c];
            for (size_t j = 0; j < 2 * Rows; ++j)
            {
                augmented[c][j] *= inverse_pivot;
            }
            for (size_t r = 0; r < Rows; ++r)
            {
                if (r == c)
                {
                    continue;
                }
                const float factor = augmented[r][c];
                if (factor == 0.0f)
                {
                    continue;
                }
                for (size_t j = 0; j < 2 * Rows; ++j)
                {
                    augmented[r][j] -= factor * augmented[c][j];
                }
            }
        }

        matrix<Rows, Rows> result; // 默认构造：列 padding 为 +0.0f
        for (size_t r = 0; r < Rows; ++r)
        {
            for (size_t c = 0; c < Rows; ++c)
            {
                result.get_element(r, c) = augmented[r][Rows + c];
            }
        }
        out = result;
        return true;
    }

    /**
     *  @brief 把逻辑元素按列主序紧凑写入调用方缓冲（跨 backend 转换与外部序列化的许可形式之一）
     *  @param m 源矩阵
     *  @param out 目标缓冲；**前提** 至少 `Rows * Cols` 个 `float` 可写
     *  @note 写出的格式是**紧凑列主序**：`out[c * Rows + r] = m(r, c)`，共 `Rows * Cols` 个值，
     *        **不含**列 padding。这个格式与 `ColumnStride`、`sizeof(matrix)` 无关，可以作为稳定的
     *        交换格式；**不得**用 `memcpy` 直接搬内部布局（padding 不是格式）。
     */
    template <size_t Rows, size_t Cols>
    ID_FORCEINLINE void pack(const matrix<Rows, Cols>& m, float* out) noexcept
    {
        for (size_t c = 0; c < Cols; ++c)
        {
            for (size_t r = 0; r < Rows; ++r)
            {
                out[c * Rows + r] = m.get_element(r, c);
            }
        }
    }

    /**
     *  @brief 从紧凑列主序缓冲读入矩阵（`pack` 的逆操作）
     *  @param src 源缓冲；**前提** 至少 `Rows * Cols` 个 `float` 可读
     *  @param out 目标矩阵；写入后逻辑元素为 `src[c * Rows + r]`，列 padding 为 `+0.0f`
     *  @note 先把结果算进临时对象再一次性赋值，因此 `&out == &m` 一类别名场景安全；
     *        源缓冲的 padding（不存在于该格式中）从不读取。
     */
    template <size_t Rows, size_t Cols>
    ID_FORCEINLINE void unpack(const float* src, matrix<Rows, Cols>& out) noexcept
    {
        matrix<Rows, Cols> result; // 默认构造：列 padding 为 +0.0f
        for (size_t c = 0; c < Cols; ++c)
        {
            for (size_t r = 0; r < Rows; ++r)
            {
                result.get_element(r, c) = src[c * Rows + r];
            }
        }
        out = result;
    }

    // 运算集合的编译期边界自证：形状可存储不等于运算可用，也不允许"按维度猜"。
    namespace detail
    {
        /// `m * n` 是否可编译
        template <typename Lhs, typename Rhs>
        concept is_multipliable_with = requires(const Lhs& lhs, const Rhs& rhs) { lhs * rhs; };

        /// `determinant(m)` 是否可编译（含 ADL）
        template <typename Value>
        concept is_determinant_taking = requires(const Value& m) { determinant(m); };

        /// `try_inverse(m, out)` 是否可编译（含 ADL）
        template <typename Value>
        concept is_invertible = requires(const Value& m, Value& out) { try_inverse(m, out); };

        /// `transpose(m)` 是否可编译（含 ADL）
        template <typename Value>
        concept is_transposable = requires(const Value& m) { transpose(m); };
    } // namespace detail

    static_assert(detail::is_determinant_taking<matrix<2, 2>> && detail::is_determinant_taking<matrix<3, 3>>
                      && detail::is_determinant_taking<matrix<4, 4>>,
                  "determinant / try_inverse 对 2 / 3 / 4 阶方阵都存在");
    static_assert(!detail::is_determinant_taking<matrix<2, 3>> && !detail::is_invertible<matrix<3, 4>>,
                  "矩形矩阵没有 determinant / try_inverse：只对 rows == cols 存在（不按维度猜）");

    static_assert(detail::is_multipliable_with<matrix<2, 3>, matrix<3, 4>>
                      && detail::is_multipliable_with<matrix<4, 4>, matrix<4, 4>>
                      && detail::is_multipliable_with<matrix<3, 3>, vector<float, 3>>,
                  "matrix<R,K> * matrix<K,C> 与 matrix<R,C> * vector<float,C> 必须存在");
    static_assert(!detail::is_multipliable_with<matrix<2, 3>, matrix<2, 3>>,
                  "维度不匹配的矩阵乘必须不存在：K 不同时模板推导失败，不做逐元素乘");
    static_assert(!detail::is_multipliable_with<matrix<2, 3>, vector<float, 4>>,
                  "matrix<R,C> * vector<float,C> 的 C 必须匹配");

    static_assert(detail::is_transposable<matrix<2, 3>> && detail::is_transposable<matrix<4, 4>>
                      && detail::is_transposable<matrix<3, 4>>,
                  "transpose 对 9 种形状都存在（含矩形与 4×4 的整 lane 路径）");
} // namespace ID::Math::SIMD
