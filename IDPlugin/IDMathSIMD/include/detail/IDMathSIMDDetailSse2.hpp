#pragma once

#include <cstddef>
#include <limits>

#include "Common/Core.hpp"
#include "Kernel/CoreTypes.hpp"

#include "detail/IDMathSIMDIsaGate.hpp" // 先过 ISA 门（x64 SSE2），再拉 intrinsic 头

#if defined(_MSC_VER)
    #include <intrin.h>
#else
    #include <emmintrin.h>
#endif

/**
 *  ID.Math.SIMD 的私有 SSE2 载入/写回助手（detail 目录不属于对外承诺）
 *  1. 本头只做两件事：把**逻辑 lane** 从存储载入 128-bit 寄存器、把寄存器里的逻辑 lane 写回存储。
 *     128-bit 寄存器值只作临时寄存器与计算中间值，不是第二份持久状态：对象里永远只有
 *     `vector::storage` / `matrix::storage` 一份可写数据；
 *  2. **没有 `reinterpret_cast`、没有 `union` punning、没有第二种类型的别名读写**：
 *     - 载入/写回用的始终是 `float*` / `const float*`，指向的真实对象就是 `float`；
 *     - 掩码用 `_mm_set_epi32` 生成整数位模式后经 `_mm_castsi128_ps` 转成浮点寄存器，两步都是
 *       编译器 intrinsic 的**值级**转换（不经过指针），因此不是别名读写，也不违反 strict aliasing；
 *  3. 对齐前提逐函数写明：`vector` 与 `matrix` 都是 `alignas(16)`，且唯一成员在偏移 0
 *     （standard layout 保证对象地址等于首成员地址），因此 `data()` 必然 16 字节对齐；对齐 load
 *     (`_mm_load_ps` / `_mm_store_ps`) 只用在这个前提成立的地方；
 *  4. **禁止越界**：`vec2` 只有 8 字节存储（其余 8 字节是 ABI 尾部填充、不可读），因此 `N == 2`
 *     一律走 `_mm_set_ps` 的两个标量读取 + 两个标量写回，绝不发 16 字节 load/store；
 *     `matrix` 的每列固定 4 个 float 槽位（16 字节），所以列载入读的 16 字节始终落在 `storage` 内；
 *  5. padding lane 在**载入时**就被掩码按位清零，因此 padding 里的 `NaN` / `Inf` 不会进入任何
 *     算术、不会污染结果；写回时再掩一次，使结果对象的 padding lane 固定为 `+0.0f`（位模式全零），
 *     与构造入口的 padding 契约一致；
 *  6. 下游只应包含伞头 `IDMathSIMDApi.hpp`；本头随实现调整，不承诺稳定性。
 */

namespace ID::Math::SIMD::detail
{
    /**
     *  @brief 前 `Lanes` 个 lane 全 1、其余 lane 全 0 的位掩码
     *  @tparam Lanes 保留的 lane 数：`1..4`
     *  @return 128-bit 掩码寄存器（保留 lane 的每一位都是 1）
     *  @note 用 `_mm_set_epi32` 造整数位模式、再用 `_mm_castsi128_ps` 取值级转换：不经过指针，
     *        因此不是 `reinterpret_cast`，也不产生别名读写。
     */
    template <size_t Lanes>
    ID_FORCEINLINE __m128 lane_mask() noexcept
    {
        static_assert(Lanes >= 1 && Lanes <= 4, "lane 掩码只对 1..4 个 lane 有意义");
        return _mm_castsi128_ps(_mm_set_epi32((Lanes > 3) ? -1 : 0,
                                              (Lanes > 2) ? -1 : 0,
                                              (Lanes > 1) ? -1 : 0,
                                              -1));
    }

    /// 符号位掩码：每个 lane 的最高位为 1，配合 `_mm_andnot_ps` 即得 IEEE-754 绝对值
    ID_FORCEINLINE __m128 sign_bits_mask() noexcept
    {
        return _mm_castsi128_ps(_mm_set1_epi32(std::numeric_limits<int>::min()));
    }

    /// 前 `Lanes` 个 lane 对应的 `_mm_movemask_ps` 位模式（`Lanes ∈ [1,4]`）
    template <size_t Lanes>
    inline constexpr int lane_presence_bits_v = (1 << static_cast<int>(Lanes)) - 1;

    /**
     *  @brief 把 `vector<float, N>` 的逻辑 lane 载入 128-bit 寄存器
     *  @tparam N 逻辑分量数：`2` / `3` / `4`
     *  @param src 指向 `storage[0]` 的指针（来自 `vector::data()`）
     *  @return lane `0..N-1` 是逻辑分量，其余 lane 为全零
     *  @note **对齐与长度前提（逐分支）**：
     *        - `N == 4`：`src` 必须 16 字节对齐（`vector` 是 `alignas(16)`、`storage` 在偏移 0，
     *          因此 `data()` 必然满足），读取恰好 16 字节；
     *        - `N == 3`：同样 16 字节对齐与 16 字节读取（`vec3` 的存储槽位就是 4 个，不越界），
     *          载入后立刻按 `lane_mask<3>()` 把 `lane3` 按位清零；
     *        - `N == 2`：只用 `_mm_set_ps` 读 `src[0]` / `src[1]` 两个**标量**，绝不发 16 字节 load：
     *          `vec2` 只有 8 字节存储，其后 8 字节是 ABI 尾部填充（不可读）。
     */
    template <size_t N>
    ID_FORCEINLINE __m128 load_lanes(const float* src) noexcept
    {
        static_assert(N >= 2 && N <= 4, "载入只对 2 / 3 / 4 个逻辑分量有意义");

        if constexpr (N == 2)
        {
            return _mm_set_ps(0.0f, 0.0f, src[1], src[0]);
        }
        else
        {
            const __m128 loaded = _mm_load_ps(src);
            if constexpr (N == 3)
            {
                return _mm_and_ps(loaded, lane_mask<3>());
            }
            else
            {
                return loaded;
            }
        }
    }

    /**
     *  @brief 把 128-bit 寄存器里的 lane `0..N-1` 写回 `vector<float, N>` 的存储
     *  @tparam N 逻辑分量数：`2` / `3` / `4`
     *  @param dst 指向 `storage[0]` 的指针（来自 `vector::data()`）
     *  @param value 待写回的值
     *  @note **长度前提（逐分支）**：`N == 2` 只写 8 字节（两个标量写，绝不对 `vec2` 发 16 字节
     *        store）；`N == 3` / `N == 4` 写恰好 16 字节，对齐前提同 `load_lanes`。
     *        `N == 3` 写回前先与 `lane_mask<3>()` 相与，使 `vec3` 的 padding lane 固定为 `+0.0f`
     *        （与构造入口一致，且保证 padding 不会再带出任何非有限值）。
     */
    template <size_t N>
    ID_FORCEINLINE void store_lanes(float* dst, __m128 value) noexcept
    {
        static_assert(N >= 2 && N <= 4, "写回只对 2 / 3 / 4 个逻辑分量有意义");

        if constexpr (N == 2)
        {
            dst[0] = _mm_cvtss_f32(value);
            dst[1] = _mm_cvtss_f32(_mm_shuffle_ps(value, value, _MM_SHUFFLE(1, 1, 1, 1)));
        }
        else if constexpr (N == 3)
        {
            _mm_store_ps(dst, _mm_and_ps(value, lane_mask<3>()));
        }
        else
        {
            _mm_store_ps(dst, value);
        }
    }

    /**
     *  @brief 载入 `matrix` 的一列（每列固定 4 个 float 槽位 = 16 字节）
     *  @tparam Rows 逻辑行数：`2` / `3` / `4`；`Rows < 4` 时槽位 `Rows..3` 是列 padding
     *  @param src 指向该列首槽位（`matrix::data() + col * ColumnStride`）
     *  @return lane `0..Rows-1` 是逻辑元素，其余 lane 为全零
     *  @note 16 字节读取**完全落在 `storage` 内**：每列自身就有 4 个槽位，最后一列的末槽位就是
     *        `storage` 的末元素，因此不存在越界读；`src` 16 字节对齐由 `matrix` 的
     *        `alignas(16)` + 列 stride 恰为 4 个 float 共同保证。
     */
    template <size_t Rows>
    ID_FORCEINLINE __m128 load_column(const float* src) noexcept
    {
        static_assert(Rows >= 2 && Rows <= 4, "列载入只对 2 / 3 / 4 行的矩阵有意义");

        const __m128 loaded = _mm_load_ps(src);
        if constexpr (Rows < 4)
        {
            return _mm_and_ps(loaded, lane_mask<Rows>());
        }
        else
        {
            return loaded;
        }
    }

    /**
     *  @brief 把一列的逻辑 lane 写回 `matrix` 的存储
     *  @tparam Rows 逻辑行数：`2` / `3` / `4`
     *  @param dst 指向该列首槽位（`matrix::data() + col * ColumnStride`）
     *  @param value 待写回的列值
     *  @note `Rows < 4` 时先与 `lane_mask<Rows>()` 相与，使列 padding 槽位固定为 `+0.0f`；
     *        写回长度固定 16 字节（每列本来就有 4 个槽位），不越界。
     */
    template <size_t Rows>
    ID_FORCEINLINE void store_column(float* dst, __m128 value) noexcept
    {
        static_assert(Rows >= 2 && Rows <= 4, "列写回只对 2 / 3 / 4 行的矩阵有意义");

        if constexpr (Rows < 4)
        {
            _mm_store_ps(dst, _mm_and_ps(value, lane_mask<Rows>()));
        }
        else
        {
            _mm_store_ps(dst, value);
        }
    }
} // namespace ID::Math::SIMD::detail
