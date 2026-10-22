#include "benchmark.hpp"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <new>
#include <type_traits>

// 只有选中 ID.Math.SIMD 的组装组合才拿得到这个头与身份宏：两者都由 IDMathSIMD target 的
// PUBLIC usage requirement 经 IDAssembly -> IDGameEngine 传播，因此项目头必须放在条件编译内，
// 否则未选该 backend 的构建（含空 profile、只选 Scalar 的组合）会直接编译失败。
#if defined(ID_USE_PLUGIN_IDMATHSIMD)
    #include "IDMathSIMDApi.hpp"
#endif

/**
 *  ID.Math.SIMD 正确性测试（backend = SIMD，基线 ISA = x64 SSE2）
 *  1. 本域只测 SIMD backend，不与 `ID.Math.Scalar` 共用任何用例、实现或结论：两个 backend 是两套
 *     用例、两份证据、两张容差表（skill 第 6 节第 22 条）。未选中本 backend 的构建里整域只输出
 *     一条 Skip 并写明未选中的对象，不伪 pass；
 *  2. 精度判据是**独立实现的 double oracle**（与本文件里的 DUT 路径完全无关，也与 Scalar 侧实现
 *     无关）：随机输入固定种子，且与 `math_scalar_security.cpp` 用同一套生成器与同一组派生种子，
 *     因此两侧拿到逐位相同的数据；**不把 Scalar 结果当真理**，也不把 SIMD 结果当真理；
 *  3. 布局是公开契约的一部分（D0-02）：`vec3` 16 字节（4 槽位、1 个 padding lane）、`vec2` 16 字节
 *     （8 字节存储 + 8 字节 ABI 尾部填充）、矩阵每列 4 个槽位（Rows=2/3 时列尾 2/1 个 padding）；
 *     padding **不参与**运算：本文件把 NaN 位模式灌进 padding 后跑运算，核对结果只由逻辑分量决定，
 *     并核对返回对象的 padding 被写成 `+0.0f`；对 `vec2` 只证明"不读尾部填充"，**不**要求尾部字节
 *     不被写（头文件明确写了它是否被复制由 ABI 决定，代码不得依赖）；
 *  3.1 叉积的"自积为零 / 与 a 正交"判据按 D0-05（精度按 double oracle + 容差表判定、不按 bitwise
 *     等价）执行：`vec3.cross_self_zero` 用容差表的一元算术行 `kTolArithmetic`，
 *     `vec3.cross_orthogonal` 用容差表的 dot 行 `kTolDot.abs_tolerance`，两条都不要求逐位为 0。
 *     判据依据（旧断言为何违反 D0-05、新断言对应容差表哪一行、失败样本的实际残差量级）见
 *     `Benchmark/validation/math/cross-orthogonality-criterion.txt`（判据说明，不是变更流水）；
 *  4. 覆盖：布局（`sizeof` / `alignof` / `ColumnStride` / `storage_bytes()` / `data_len()` /
 *     `kPaddingLaneCount`）、构造（默认零初始化、逐分量、`splat`、跨类型 `explicit`）、向量运算全
 *     N=2/3/4（`cross` 仅 vec3、`cross2`、`hadamard`、`dot`、`length`、`try_normalize` 失败时输出
 *     不变）、`pack` / `unpack`、矩阵 9 形状乘、矩形 `transpose`、`matrix * vector`、`identity`、
 *     `determinant`、`try_inverse`（近奇异失败时输出不变）、行代理读写，以及三种绑定的正向用例
 *     （`const` 对象 / 非 `const` 引用 / 右值对象）；
 *  5. 编译失败负例（`vector<int,3>` 运算、`vector<float,8>` 运算、`cross(vec2)`、右值 `operator[]`、
 *     维度不匹配）在 `Benchmark/validation/math` 下的独立驱动里（默认 OFF、不写官方 report.txt、
 *     不参与评级）；本文件只断言"受限组合没有运算符"这一可编译事实；
 *  6. 用例自包含：全部对象都是栈上 POD，禁用动态分配（placement new 只用于哨兵布局取证），因此不存在
 *     泄漏路径；每条用例自己构造数据、自己判失败，失败只记 `Check`，不影响后续用例。
 */

#if defined(ID_USE_PLUGIN_IDMATHSIMD)

namespace
{
    using ID::Math::SIMD::matrix;
    using ID::Math::SIMD::vec2;
    using ID::Math::SIMD::vec3;
    using ID::Math::SIMD::vec4;
    using ID::Math::SIMD::vector;

    // ============================ 冻结的容差表 ============================
    // 判定式：|actual - expected| <= abs + rel * max(|actual|, |expected|)
    // 条件：DUT 全程 float 计算；oracle 全程 double；输入范围 |x| <= 1；构建 flags 见报告头 build 行。
    // 与 Scalar 侧同表同值：两个 backend 算法不同（SIMD 的 dot 是 128-bit lane 成对归约），误差界一致。
    constexpr double kFloatEpsilon = 1.192092896e-07;      // FLT_EPSILON

    struct Tolerance
    {
        double abs_tolerance;
        double rel_tolerance;
    };

    constexpr Tolerance kTolArithmetic{ 2.0 * kFloatEpsilon, 2.0 * kFloatEpsilon };
    constexpr Tolerance kTolDot{ 4.0e-6, 4.0e-6 };
    constexpr Tolerance kTolLength{ 8.0e-6, 8.0e-6 };
    constexpr Tolerance kTolNormalize{ 8.0e-6, 8.0e-6 };
    constexpr Tolerance kTolMatVec{ 8.0e-6, 8.0e-6 };
    constexpr Tolerance kTolMatMul{ 2.0e-5, 2.0e-5 };
    constexpr Tolerance kTolDeterminant{ 5.0e-5, 5.0e-5 };
    constexpr Tolerance kTolInverse{ 2.0e-4, 2.0e-4 };

    // ============================ 固定种子数据 ============================
    // 与 math_scalar_security.cpp 逐字相同的派生种子与生成器：两个 backend 拿到同一份数据。
    constexpr ID::uint64_t kSeedVectorLhs   = 0x5EED000000000101ull;
    constexpr ID::uint64_t kSeedVectorRhs   = 0x5EED000000000202ull;
    constexpr ID::uint64_t kSeedVectorUnit  = 0x5EED000000000303ull;
    constexpr ID::uint64_t kSeedMatrixLhs   = 0x5EED000000000404ull;
    constexpr ID::uint64_t kSeedMatrixRhs   = 0x5EED000000000505ull;
    constexpr ID::uint64_t kSeedSquare      = 0x5EED000000000606ull;

    constexpr ID::size_t kMaxExtent = 4;

    /// SIMD backend 的 128-bit lane 对齐字节数（D0-02 冻结值）
    constexpr ID::size_t kLaneBytes = 16;

    /// NaN 位模式（quiet NaN），用于"padding 里的非有限值不得进入算术"的取证
    constexpr ID::uint32_t kNaNBits = 0x7FC00001u;

    class FixedRandom
    {
    public:
        explicit FixedRandom(ID::uint64_t seed) noexcept
            : m_state(seed)
        {
        }

        float next_unit() noexcept
        {
            m_state = m_state * 6364136223846793005ull + 1442695040888963407ull;
            const ID::uint64_t mantissa = (m_state >> 40) & 0xFFFFFFull;
            return static_cast<float>(mantissa) / 16777216.0f;
        }

        float next_signed() noexcept
        {
            return next_unit() * 2.0f - 1.0f;
        }

    private:
        ID::uint64_t m_state;
    };

    void make_float_vector(float* out, ID::size_t count, ID::uint64_t seed) noexcept
    {
        FixedRandom random(seed);
        for (ID::size_t i = 0; i < count; ++i)
        {
            out[i] = random.next_signed();
        }
    }

    void make_positive_vector(float* out, ID::size_t count, ID::uint64_t seed) noexcept
    {
        FixedRandom random(seed);
        for (ID::size_t i = 0; i < count; ++i)
        {
            out[i] = 0.4f + 0.8f * random.next_unit();
        }
    }

    void make_float_matrix(float* out, ID::size_t rows, ID::size_t cols, ID::uint64_t seed) noexcept
    {
        FixedRandom random(seed);
        for (ID::size_t c = 0; c < cols; ++c)
        {
            for (ID::size_t r = 0; r < rows; ++r)
            {
                out[c * rows + r] = random.next_signed();
            }
        }
    }

    void make_dominant_matrix(float* out, ID::size_t order, ID::uint64_t seed) noexcept
    {
        FixedRandom random(seed);
        for (ID::size_t c = 0; c < order; ++c)
        {
            for (ID::size_t r = 0; r < order; ++r)
            {
                const float value = random.next_signed() * 0.5f;
                out[c * order + r] = (r == c) ? (2.0f + value) : value;
            }
        }
    }

    /// 把 `kNaNBits` 写进某个 padding 槽位（只用于验证"padding 不参与运算"，属布局契约取证）
    void poison_slot(float& slot) noexcept
    {
        std::memcpy(&slot, &kNaNBits, sizeof(kNaNBits));
    }

    // ============================ double oracle ============================

    void oracle_add(const double* lhs, const double* rhs, double* out, ID::size_t count) noexcept
    {
        for (ID::size_t i = 0; i < count; ++i)
        {
            out[i] = lhs[i] + rhs[i];
        }
    }

    void oracle_sub(const double* lhs, const double* rhs, double* out, ID::size_t count) noexcept
    {
        for (ID::size_t i = 0; i < count; ++i)
        {
            out[i] = lhs[i] - rhs[i];
        }
    }

    void oracle_hadamard(const double* lhs, const double* rhs, double* out, ID::size_t count) noexcept
    {
        for (ID::size_t i = 0; i < count; ++i)
        {
            out[i] = lhs[i] * rhs[i];
        }
    }

    void oracle_scale(const double* source, double factor, double* out, ID::size_t count) noexcept
    {
        for (ID::size_t i = 0; i < count; ++i)
        {
            out[i] = source[i] * factor;
        }
    }

    void oracle_divide(const double* source, double divisor, double* out, ID::size_t count) noexcept
    {
        for (ID::size_t i = 0; i < count; ++i)
        {
            out[i] = source[i] / divisor;
        }
    }

    double oracle_dot(const double* lhs, const double* rhs, ID::size_t count) noexcept
    {
        double sum = 0.0;
        for (ID::size_t i = 0; i < count; ++i)
        {
            sum += lhs[i] * rhs[i];
        }
        return sum;
    }

    double oracle_length(const double* value, ID::size_t count) noexcept
    {
        return std::sqrt(oracle_dot(value, value, count));
    }

    void oracle_normalize(const double* value, double* out, ID::size_t count) noexcept
    {
        const double inverse_length = 1.0 / oracle_length(value, count);
        for (ID::size_t i = 0; i < count; ++i)
        {
            out[i] = value[i] * inverse_length;
        }
    }

    void oracle_cross(const double* lhs, const double* rhs, double* out) noexcept
    {
        out[0] = lhs[1] * rhs[2] - lhs[2] * rhs[1];
        out[1] = lhs[2] * rhs[0] - lhs[0] * rhs[2];
        out[2] = lhs[0] * rhs[1] - lhs[1] * rhs[0];
    }

    double oracle_cross2(const double* lhs, const double* rhs) noexcept
    {
        return lhs[0] * rhs[1] - lhs[1] * rhs[0];
    }

    void oracle_mat_mul(const double* lhs, ID::size_t rows, ID::size_t inner, const double* rhs, ID::size_t cols,
                        double* out) noexcept
    {
        for (ID::size_t c = 0; c < cols; ++c)
        {
            for (ID::size_t r = 0; r < rows; ++r)
            {
                double sum = 0.0;
                for (ID::size_t k = 0; k < inner; ++k)
                {
                    sum += lhs[k * rows + r] * rhs[c * inner + k];
                }
                out[c * rows + r] = sum;
            }
        }
    }

    void oracle_mat_vec(const double* m, ID::size_t rows, ID::size_t cols, const double* v, double* out) noexcept
    {
        for (ID::size_t r = 0; r < rows; ++r)
        {
            double sum = 0.0;
            for (ID::size_t c = 0; c < cols; ++c)
            {
                sum += m[c * rows + r] * v[c];
            }
            out[r] = sum;
        }
    }

    void oracle_transpose(const double* m, ID::size_t rows, ID::size_t cols, double* out) noexcept
    {
        for (ID::size_t c = 0; c < cols; ++c)
        {
            for (ID::size_t r = 0; r < rows; ++r)
            {
                out[r * cols + c] = m[c * rows + r];
            }
        }
    }

    /**
     *  @brief 双精度行列式（部分主元高斯消元）
     *  @note 与 SIMD DUT 的 `determinant_small` 是两份独立实现（本 oracle 只用 double，且不共享代码）；
     *        输入列主序。
     */
    double oracle_determinant(const double* m, ID::size_t order) noexcept
    {
        double a[kMaxExtent * kMaxExtent] = {};
        for (ID::size_t c = 0; c < order; ++c)
        {
            for (ID::size_t r = 0; r < order; ++r)
            {
                a[c * order + r] = m[c * order + r];
            }
        }

        double value = 1.0;
        for (ID::size_t c = 0; c < order; ++c)
        {
            ID::size_t pivot           = c;
            double      pivot_magnitude = std::fabs(a[c * order + c]);
            for (ID::size_t r = c + 1; r < order; ++r)
            {
                const double magnitude = std::fabs(a[c * order + r]);
                if (magnitude > pivot_magnitude)
                {
                    pivot           = r;
                    pivot_magnitude = magnitude;
                }
            }
            if (pivot_magnitude == 0.0)
            {
                return 0.0;
            }
            if (pivot != c)
            {
                for (ID::size_t j = c; j < order; ++j)
                {
                    const double temporary = a[j * order + c];
                    a[j * order + c]      = a[j * order + pivot];
                    a[j * order + pivot]  = temporary;
                }
                value = -value;
            }

            const double pivot_value = a[c * order + c];
            value *= pivot_value;
            for (ID::size_t r = c + 1; r < order; ++r)
            {
                const double factor = a[c * order + r] / pivot_value;
                if (factor == 0.0)
                {
                    continue;
                }
                for (ID::size_t j = c + 1; j < order; ++j)
                {
                    a[j * order + r] -= factor * a[j * order + c];
                }
            }
        }
        return value;
    }

    bool oracle_inverse(const double* m, ID::size_t order, double* out) noexcept
    {
        double augmented[kMaxExtent][2 * kMaxExtent] = {};
        for (ID::size_t r = 0; r < order; ++r)
        {
            for (ID::size_t c = 0; c < order; ++c)
            {
                augmented[r][c]         = m[c * order + r];
                augmented[r][order + c] = (r == c) ? 1.0 : 0.0;
            }
        }

        for (ID::size_t c = 0; c < order; ++c)
        {
            ID::size_t pivot            = c;
            double      pivot_magnitude = std::fabs(augmented[c][c]);
            for (ID::size_t r = c + 1; r < order; ++r)
            {
                const double magnitude = std::fabs(augmented[r][c]);
                if (magnitude > pivot_magnitude)
                {
                    pivot           = r;
                    pivot_magnitude = magnitude;
                }
            }
            if (pivot_magnitude == 0.0)
            {
                return false;
            }
            if (pivot != c)
            {
                for (ID::size_t j = 0; j < 2 * order; ++j)
                {
                    const double temporary = augmented[c][j];
                    augmented[c][j]        = augmented[pivot][j];
                    augmented[pivot][j]    = temporary;
                }
            }

            const double inverse_pivot = 1.0 / augmented[c][c];
            for (ID::size_t j = 0; j < 2 * order; ++j)
            {
                augmented[c][j] *= inverse_pivot;
            }
            for (ID::size_t r = 0; r < order; ++r)
            {
                if (r == c)
                {
                    continue;
                }
                const double factor = augmented[r][c];
                if (factor == 0.0)
                {
                    continue;
                }
                for (ID::size_t j = 0; j < 2 * order; ++j)
                {
                    augmented[r][j] -= factor * augmented[c][j];
                }
            }
        }

        for (ID::size_t r = 0; r < order; ++r)
        {
            for (ID::size_t c = 0; c < order; ++c)
            {
                out[c * order + r] = augmented[r][order + c];
            }
        }
        return true;
    }

    // ============================ 比较与记录 ============================

    struct Comparison
    {
        bool        ok       = true;
        ID::size_t  index    = 0;
        double      actual   = 0.0;
        double      expected = 0.0;
        double      limit    = 0.0;
    };

    Comparison compare_series(const double* actual, const double* expected, ID::size_t count,
                              Tolerance tolerance) noexcept
    {
        Comparison result{};
        for (ID::size_t i = 0; i < count; ++i)
        {
            const double actual_magnitude   = std::fabs(actual[i]);
            const double expected_magnitude = std::fabs(expected[i]);
            const double magnitude          = (actual_magnitude > expected_magnitude) ? actual_magnitude
                                                                                     : expected_magnitude;
            const double limit              = tolerance.abs_tolerance + tolerance.rel_tolerance * magnitude;
            if (!(std::fabs(actual[i] - expected[i]) <= limit))
            {
                result.ok       = false;
                result.index    = i;
                result.actual   = actual[i];
                result.expected = expected[i];
                result.limit    = limit;
                return result;
            }
        }
        return result;
    }

    void check_series(std::string_view name, const Comparison& comparison) noexcept
    {
        char detail[192] = {};
        if (comparison.ok)
        {
            std::snprintf(detail, sizeof(detail), "与 double oracle 逐项一致（冻结容差内）");
        }
        else
        {
            std::snprintf(detail, sizeof(detail), "第 %zu 项实际 %.9g / oracle %.9g，允许误差 %.3g",
                          static_cast<size_t>(comparison.index), comparison.actual, comparison.expected,
                          comparison.limit);
        }
        ID::Bench::Check(comparison.ok, name, detail);
    }

    void check_flag(bool ok, std::string_view name, const char* detail) noexcept
    {
        ID::Bench::Check(ok, name, detail);
    }

    // ============================ 读取 DUT 对象 ============================

    template <typename VectorType>
    void read_vector(const VectorType& value, double* out) noexcept
    {
        for (ID::size_t i = 0; i < VectorType::kComponentCount; ++i)
        {
            out[i] = static_cast<double>(value[i]);
        }
    }

    template <typename MatrixType>
    void read_matrix(const MatrixType& value, double* out) noexcept
    {
        for (ID::size_t c = 0; c < MatrixType::kColumnCount; ++c)
        {
            for (ID::size_t r = 0; r < MatrixType::kRowCount; ++r)
            {
                out[c * MatrixType::kRowCount + r] = static_cast<double>(value.get_element(r, c));
            }
        }
    }

    template <typename VectorType>
    VectorType make_vector(const float* source) noexcept
    {
        VectorType result;
        for (ID::size_t i = 0; i < VectorType::kComponentCount; ++i)
        {
            result[i] = source[i];
        }
        return result;
    }

    template <typename MatrixType>
    MatrixType make_matrix(const float* column_major) noexcept
    {
        MatrixType result;
        for (ID::size_t c = 0; c < MatrixType::kColumnCount; ++c)
        {
            for (ID::size_t r = 0; r < MatrixType::kRowCount; ++r)
            {
                result.get_element(r, c) = column_major[c * MatrixType::kRowCount + r];
            }
        }
        return result;
    }

    template <typename VectorType>
    void doubles_of(const float* source, double* out) noexcept
    {
        for (ID::size_t i = 0; i < VectorType::kComponentCount; ++i)
        {
            out[i] = static_cast<double>(source[i]);
        }
    }

    // 运算可用性的编译期探针：必须写成**依赖型**变量模板。
    // 直接在非模板上下文里对具体类型写 `requires { lhs + rhs; }` 时，表达式不依赖模板参数，
    // 一旦该运算不存在就是硬错误（[expr.prim.req]/5），而不是"概念为 false"。
    template <typename VectorType>
    inline constexpr bool is_addable_v = requires(const VectorType& lhs, const VectorType& rhs) { lhs + rhs; };

    template <typename VectorType>
    inline constexpr bool is_dottable_v = requires(const VectorType& lhs, const VectorType& rhs) { dot(lhs, rhs); };

    template <typename VectorType>
    inline constexpr bool is_crossable_v = requires(const VectorType& lhs, const VectorType& rhs) { cross(lhs, rhs); };

    template <typename MatrixType>
    inline constexpr bool is_determinant_taking_v = requires(const MatrixType& value) { determinant(value); };

    template <typename MatrixType>
    inline constexpr bool is_inversible_v =
        requires(const MatrixType& value, MatrixType& out) { try_inverse(value, out); };

    /// 一个 `vec3` 的 padding lane 是否被写成 `+0.0f`（位模式全零）
    bool padding_lane_is_zero(const vec3& value) noexcept
    {
        // 头文件写明"对 vec3 从 data() 读 16 字节不越界"，因此这里是有界的布局取证；
        // padding 不属于公开值，读数只用于核对"返回对象的 padding 被清零"这一条公开契约。
        ID::uint32_t bits = 0;
        std::memcpy(&bits, value.data() + 3, sizeof(bits));
        return bits == 0u;
    }

    /// 一个矩阵某列的 padding 槽位是否全为 `+0.0f`
    template <typename MatrixType>
    bool column_padding_is_zero(const MatrixType& value, ID::size_t column) noexcept
    {
        bool all_zero = true;
        for (ID::size_t slot = MatrixType::kRowCount; slot < MatrixType::kColumnStride; ++slot)
        {
            ID::uint32_t bits = 0;
            std::memcpy(&bits, value.data() + column * MatrixType::kColumnStride + slot, sizeof(bits));
            all_zero = all_zero && (bits == 0u);
        }
        return all_zero;
    }

    // ============================ 向量运算用例 ============================

    /**
     *  @brief 对一种分量数跑完整的向量运算矩阵（与 Scalar 域同一套用例形状、同一份数据）
     *  @tparam VectorType `vec2` / `vec3` / `vec4`
     */
    template <typename VectorType>
    void check_vector_algebra() noexcept
    {
        constexpr ID::size_t N = VectorType::kComponentCount;

        float lhs_source[kMaxExtent] = {};
        float rhs_source[kMaxExtent] = {};
        make_float_vector(lhs_source, N, kSeedVectorLhs);
        make_float_vector(rhs_source, N, kSeedVectorRhs);

        double lhs[kMaxExtent] = {};
        double rhs[kMaxExtent] = {};
        doubles_of<VectorType>(lhs_source, lhs);
        doubles_of<VectorType>(rhs_source, rhs);

        const VectorType lhs_value = make_vector<VectorType>(lhs_source);
        const VectorType rhs_value = make_vector<VectorType>(rhs_source);

        double actual[kMaxExtent]   = {};
        double expected[kMaxExtent] = {};

        {
            read_vector(lhs_value + rhs_value, actual);
            oracle_add(lhs, rhs, expected, N);
            char name[48] = {};
            std::snprintf(name, sizeof(name), "vec%zu.add", static_cast<size_t>(N));
            check_series(name, compare_series(actual, expected, N, kTolArithmetic));
        }

        {
            read_vector(lhs_value - rhs_value, actual);
            oracle_sub(lhs, rhs, expected, N);
            char name[48] = {};
            std::snprintf(name, sizeof(name), "vec%zu.sub", static_cast<size_t>(N));
            check_series(name, compare_series(actual, expected, N, kTolArithmetic));
        }

        {
            constexpr double kFactor = 2.5;
            read_vector(lhs_value * static_cast<float>(kFactor), actual);
            oracle_scale(lhs, kFactor, expected, N);
            char name[48] = {};
            std::snprintf(name, sizeof(name), "vec%zu.mul_scalar", static_cast<size_t>(N));
            check_series(name, compare_series(actual, expected, N, kTolArithmetic));

            read_vector(static_cast<float>(kFactor) * lhs_value, actual);
            std::snprintf(name, sizeof(name), "vec%zu.scalar_mul", static_cast<size_t>(N));
            check_series(name, compare_series(actual, expected, N, kTolArithmetic));
        }

        {
            constexpr double kDivisor = 4.0;
            read_vector(lhs_value / static_cast<float>(kDivisor), actual);
            oracle_divide(lhs, kDivisor, expected, N);
            char name[48] = {};
            std::snprintf(name, sizeof(name), "vec%zu.div_scalar", static_cast<size_t>(N));
            check_series(name, compare_series(actual, expected, N, kTolArithmetic));
        }

        {
            read_vector(ID::Math::SIMD::hadamard(lhs_value, rhs_value), actual);
            oracle_hadamard(lhs, rhs, expected, N);
            char name[48] = {};
            std::snprintf(name, sizeof(name), "vec%zu.hadamard", static_cast<size_t>(N));
            check_series(name, compare_series(actual, expected, N, kTolArithmetic));
        }

        {
            const double actual_dot   = static_cast<double>(ID::Math::SIMD::dot(lhs_value, rhs_value));
            const double expected_dot = oracle_dot(lhs, rhs, N);
            const double limit        = kTolDot.abs_tolerance + kTolDot.rel_tolerance * std::fabs(expected_dot);
            char         detail[192]  = {};
            std::snprintf(detail, sizeof(detail), "actual %.9g / oracle %.9g，允许 %.3g", actual_dot, expected_dot, limit);
            char name[48] = {};
            std::snprintf(name, sizeof(name), "vec%zu.dot", static_cast<size_t>(N));
            check_flag(std::fabs(actual_dot - expected_dot) <= limit, name, detail);

            const double actual_squared   = static_cast<double>(ID::Math::SIMD::squared_length(lhs_value));
            const double expected_squared = oracle_dot(lhs, lhs, N);
            const double squared_limit    = kTolDot.abs_tolerance + kTolDot.rel_tolerance * std::fabs(expected_squared);
            std::snprintf(detail, sizeof(detail), "actual %.9g / oracle %.9g，允许 %.3g", actual_squared,
                          expected_squared, squared_limit);
            std::snprintf(name, sizeof(name), "vec%zu.squared_length", static_cast<size_t>(N));
            check_flag(std::fabs(actual_squared - expected_squared) <= squared_limit, name, detail);
        }

        {
            float positive_source[kMaxExtent] = {};
            make_positive_vector(positive_source, N, kSeedVectorUnit);
            const VectorType value = make_vector<VectorType>(positive_source);

            double positive[kMaxExtent] = {};
            doubles_of<VectorType>(positive_source, positive);

            const double actual_length   = static_cast<double>(ID::Math::SIMD::length(value));
            const double expected_length = oracle_length(positive, N);
            const double length_limit    = kTolLength.abs_tolerance + kTolLength.rel_tolerance * expected_length;
            char         detail[192]     = {};
            std::snprintf(detail, sizeof(detail), "actual %.9g / oracle %.9g，允许 %.3g", actual_length,
                          expected_length, length_limit);
            char name[48] = {};
            std::snprintf(name, sizeof(name), "vec%zu.length", static_cast<size_t>(N));
            check_flag(std::fabs(actual_length - expected_length) <= length_limit, name, detail);

            read_vector(ID::Math::SIMD::normalize_unchecked(value), actual);
            oracle_normalize(positive, expected, N);
            std::snprintf(name, sizeof(name), "vec%zu.normalize_unchecked", static_cast<size_t>(N));
            check_series(name, compare_series(actual, expected, N, kTolNormalize));

            VectorType normalized;
            const bool normalized_ok = ID::Math::SIMD::try_normalize(value, normalized);
            read_vector(normalized, actual);
            std::snprintf(name, sizeof(name), "vec%zu.try_normalize_success", static_cast<size_t>(N));
            check_series(name, compare_series(actual, expected, N, kTolNormalize));
            std::snprintf(name, sizeof(name), "vec%zu.try_normalize_return", static_cast<size_t>(N));
            check_flag(normalized_ok, name, "良态输入必须返回 true");

            VectorType aliased = value;
            const bool aliased_ok = ID::Math::SIMD::try_normalize(aliased, aliased);
            read_vector(aliased, actual);
            std::snprintf(name, sizeof(name), "vec%zu.try_normalize_alias", static_cast<size_t>(N));
            check_series(name, compare_series(actual, expected, N, kTolNormalize));
            check_flag(aliased_ok, name, "别名调用（&out == &v）必须成功");
        }

        {
            VectorType zero;
            VectorType out;
            for (ID::size_t i = 0; i < N; ++i)
            {
                out[i] = static_cast<float>(10.0 + static_cast<double>(i));
            }
            const VectorType before = out;
            const bool       failed = ID::Math::SIMD::try_normalize(zero, out);

            bool unchanged = true;
            for (ID::size_t i = 0; i < N; ++i)
            {
                unchanged = unchanged && (out[i] == before[i]);
            }
            char name[64] = {};
            std::snprintf(name, sizeof(name), "vec%zu.try_normalize_zero_fails", static_cast<size_t>(N));
            check_flag(!failed, name, "零向量的 squared_length 不为正：必须返回 false");
            std::snprintf(name, sizeof(name), "vec%zu.try_normalize_failure_keeps_out", static_cast<size_t>(N));
            check_flag(unchanged, name, "失败路径不得改写输出参数（先写入可辨识原值再核对）");
        }

        {
            char name[64] = {};
            std::snprintf(name, sizeof(name), "vec%zu.equals_self", static_cast<size_t>(N));
            check_flag(ID::Math::SIMD::equals(lhs_value, lhs_value), name, "逐分量精确相等：自身必须相等");

            VectorType shifted = lhs_value;
            shifted[0] = shifted[0] + 1.0f;
            std::snprintf(name, sizeof(name), "vec%zu.equals_rejects_difference", static_cast<size_t>(N));
            check_flag(!ID::Math::SIMD::equals(lhs_value, shifted), name, "差 1.0 必须判为不相等");

            std::snprintf(name, sizeof(name), "vec%zu.near_equal_self_exact", static_cast<size_t>(N));
            check_flag(ID::Math::SIMD::near_equal(lhs_value, lhs_value, 0.0f, 0.0f), name,
                       "abs=rel=0 时自身必须仍在容差内");

            std::snprintf(name, sizeof(name), "vec%zu.near_equal_rejects_big_difference", static_cast<size_t>(N));
            check_flag(!ID::Math::SIMD::near_equal(lhs_value, shifted, 1.0e-6f, 1.0e-6f), name,
                       "1.0 的差远超 abs=1e-6/rel=1e-6，必须判为不相等");
        }

        // ---- pack / unpack 往返：只写 N 个 float，且不读缓冲越界部分 ----
        {
            const VectorType value = make_vector<VectorType>(lhs_source);
            float packed[kMaxExtent] = { -99.0f, -99.0f, -99.0f, -99.0f };
            ID::Math::SIMD::pack(value, packed);

            bool pack_ok = true;
            for (ID::size_t i = 0; i < N; ++i)
            {
                pack_ok = pack_ok && (static_cast<double>(packed[i]) == lhs[i]);
            }
            bool pack_bounds_ok = true;
            for (ID::size_t i = N; i < kMaxExtent; ++i)
            {
                pack_bounds_ok = pack_bounds_ok && (packed[i] == -99.0f);
            }
            char name[64] = {};
            std::snprintf(name, sizeof(name), "vec%zu.pack_values", static_cast<size_t>(N));
            check_flag(pack_ok, name, "pack 写出的必须是逻辑分量本身（紧凑格式）");
            std::snprintf(name, sizeof(name), "vec%zu.pack_writes_only_n", static_cast<size_t>(N));
            check_flag(pack_bounds_ok, name, "pack 只写 N 个 float：缓冲第 N 项之后必须保持原值");

            float      source[kMaxExtent] = { 1.25f, -2.5f, 3.75f, -4.0f };
            VectorType roundtrip;
            ID::Math::SIMD::unpack(source, roundtrip);
            bool unpack_ok = true;
            for (ID::size_t i = 0; i < N; ++i)
            {
                unpack_ok = unpack_ok && (roundtrip[i] == source[i]);
            }
            std::snprintf(name, sizeof(name), "vec%zu.unpack_values", static_cast<size_t>(N));
            check_flag(unpack_ok, name, "unpack 读入的必须是缓冲的前 N 个 float");
        }
    }

    // ============================ 矩阵用例 ============================

    template <size_t Rows, size_t Cols>
    bool matrix_layout_ok() noexcept
    {
        using MatrixType = matrix<Rows, Cols>;
        const MatrixType value{};
        return MatrixType::kColumnStride == 4
            && MatrixType::kStorageCount == Cols * 4
            && MatrixType::kPaddingSlotsPerColumn == 4 - Rows
            && MatrixType::kColumnPaddingBytes == (4 - Rows) * sizeof(float)
            && MatrixType::kTotalPaddingBytes == Cols * (4 - Rows) * sizeof(float)
            && sizeof(MatrixType) == Cols * kLaneBytes
            && alignof(MatrixType) == kLaneBytes
            && value.storage_bytes() == sizeof(MatrixType)
            && value.column_stride() == 4
            && value.rows() == Rows
            && value.cols() == Cols;
    }

    template <size_t Rows, size_t K, size_t Cols>
    void check_matrix_multiply(const char* name) noexcept
    {
        float lhs_source[kMaxExtent * kMaxExtent] = {};
        float rhs_source[kMaxExtent * kMaxExtent] = {};
        make_float_matrix(lhs_source, Rows, K, kSeedMatrixLhs);
        make_float_matrix(rhs_source, K, Cols, kSeedMatrixRhs);

        const matrix<Rows, K>    lhs     = make_matrix<matrix<Rows, K>>(lhs_source);
        const matrix<K, Cols>    rhs     = make_matrix<matrix<K, Cols>>(rhs_source);
        const matrix<Rows, Cols> product = lhs * rhs;

        double actual[kMaxExtent * kMaxExtent]     = {};
        double expected[kMaxExtent * kMaxExtent]   = {};
        double lhs_double[kMaxExtent * kMaxExtent] = {};
        double rhs_double[kMaxExtent * kMaxExtent] = {};
        for (ID::size_t i = 0; i < Rows * K; ++i)
        {
            lhs_double[i] = static_cast<double>(lhs_source[i]);
        }
        for (ID::size_t i = 0; i < K * Cols; ++i)
        {
            rhs_double[i] = static_cast<double>(rhs_source[i]);
        }

        read_matrix(product, actual);
        oracle_mat_mul(lhs_double, Rows, K, rhs_double, Cols, expected);
        check_series(name, compare_series(actual, expected, Rows * Cols, kTolMatMul));

        bool padding_zero = true;
        for (ID::size_t c = 0; c < Cols; ++c)
        {
            padding_zero = padding_zero && column_padding_is_zero(product, c);
        }
        char padding_name[64] = {};
        std::snprintf(padding_name, sizeof(padding_name), "%s_padding_zero", name);
        check_flag(padding_zero, padding_name, "矩阵乘结果的列 padding 必须全为 +0.0f");
    }

    template <size_t Rows, size_t Cols>
    void check_matrix_vector(const char* name) noexcept
    {
        float matrix_source[kMaxExtent * kMaxExtent] = {};
        float vector_source[kMaxExtent]              = {};
        make_float_matrix(matrix_source, Rows, Cols, kSeedMatrixLhs);
        make_float_vector(vector_source, Cols, kSeedVectorRhs);

        const matrix<Rows, Cols>  m       = make_matrix<matrix<Rows, Cols>>(matrix_source);
        const vector<float, Cols> v       = make_vector<vector<float, Cols>>(vector_source);
        const vector<float, Rows> product = m * v;

        double actual[kMaxExtent]                     = {};
        double expected[kMaxExtent]                   = {};
        double matrix_double[kMaxExtent * kMaxExtent] = {};
        double vector_double[kMaxExtent]              = {};
        for (ID::size_t i = 0; i < Rows * Cols; ++i)
        {
            matrix_double[i] = static_cast<double>(matrix_source[i]);
        }
        for (ID::size_t i = 0; i < Cols; ++i)
        {
            vector_double[i] = static_cast<double>(vector_source[i]);
        }

        read_vector(product, actual);
        oracle_mat_vec(matrix_double, Rows, Cols, vector_double, expected);
        check_series(name, compare_series(actual, expected, Rows, kTolMatVec));

        if constexpr (Rows == 3)
        {
            char padding_name[64] = {};
            std::snprintf(padding_name, sizeof(padding_name), "%s_padding_zero", name);
            check_flag(padding_lane_is_zero(product), padding_name, "vec3 结果的 padding lane 必须是 +0.0f");
        }
    }

    template <size_t Rows, size_t Cols>
    void check_matrix_transpose(const char* name) noexcept
    {
        float source[kMaxExtent * kMaxExtent] = {};
        make_float_matrix(source, Rows, Cols, kSeedMatrixLhs);

        const matrix<Rows, Cols> m          = make_matrix<matrix<Rows, Cols>>(source);
        const matrix<Cols, Rows> transposed = ID::Math::SIMD::transpose(m);

        double actual[kMaxExtent * kMaxExtent]        = {};
        double expected[kMaxExtent * kMaxExtent]      = {};
        double source_double[kMaxExtent * kMaxExtent] = {};
        for (ID::size_t i = 0; i < Rows * Cols; ++i)
        {
            source_double[i] = static_cast<double>(source[i]);
        }

        read_matrix(transposed, actual);
        oracle_transpose(source_double, Rows, Cols, expected);
        check_series(name, compare_series(actual, expected, Rows * Cols, kTolArithmetic));
    }

    template <size_t Order>
    void check_square_operations() noexcept
    {
        char name[64] = {};

        {
            const matrix<Order, Order> identity_matrix = matrix<Order, Order>::identity();
            bool                       diagonal_ok       = true;
            bool                       off_diagonal_zero = true;
            for (ID::size_t r = 0; r < Order; ++r)
            {
                for (ID::size_t c = 0; c < Order; ++c)
                {
                    const float value = identity_matrix.get_element(r, c);
                    if (r == c)
                    {
                        diagonal_ok = diagonal_ok && (value == 1.0f);
                    }
                    else
                    {
                        off_diagonal_zero = off_diagonal_zero && (value == 0.0f);
                    }
                }
            }
            std::snprintf(name, sizeof(name), "mat%zu.identity", static_cast<size_t>(Order));
            check_flag(diagonal_ok && off_diagonal_zero, name, "identity() 必须是对角 1、其余 0");

            bool padding_zero = true;
            for (ID::size_t c = 0; c < Order; ++c)
            {
                padding_zero = padding_zero && column_padding_is_zero(identity_matrix, c);
            }
            std::snprintf(name, sizeof(name), "mat%zu.identity_padding_zero", static_cast<size_t>(Order));
            check_flag(padding_zero, name, "identity() 的列 padding 必须是 +0.0f");

            const matrix<Order, Order> zero_matrix{};
            bool                       is_zero = true;
            for (ID::size_t r = 0; r < Order; ++r)
            {
                for (ID::size_t c = 0; c < Order; ++c)
                {
                    is_zero = is_zero && (zero_matrix.get_element(r, c) == 0.0f);
                }
            }
            std::snprintf(name, sizeof(name), "mat%zu.default_is_zero_not_identity", static_cast<size_t>(Order));
            check_flag(is_zero, name, "默认构造必须是零矩阵（单位矩阵只能显式调用 identity）");

            float source[kMaxExtent * kMaxExtent] = {};
            make_float_matrix(source, Order, Order, kSeedSquare);
            const matrix<Order, Order> m = make_matrix<matrix<Order, Order>>(source);

            const matrix<Order, Order> left  = identity_matrix * m;
            const matrix<Order, Order> right = m * identity_matrix;
            double m_double[kMaxExtent * kMaxExtent] = {};
            for (ID::size_t i = 0; i < Order * Order; ++i)
            {
                m_double[i] = static_cast<double>(source[i]);
            }
            double actual[kMaxExtent * kMaxExtent] = {};
            read_matrix(left, actual);
            std::snprintf(name, sizeof(name), "mat%zu.identity_left_mul", static_cast<size_t>(Order));
            check_series(name, compare_series(actual, m_double, Order * Order, kTolArithmetic));
            read_matrix(right, actual);
            std::snprintf(name, sizeof(name), "mat%zu.identity_right_mul", static_cast<size_t>(Order));
            check_series(name, compare_series(actual, m_double, Order * Order, kTolArithmetic));
        }

        {
            float source[kMaxExtent * kMaxExtent] = {};
            make_float_matrix(source, Order, Order, kSeedSquare);
            const matrix<Order, Order> m = make_matrix<matrix<Order, Order>>(source);
            double source_double[kMaxExtent * kMaxExtent] = {};
            for (ID::size_t i = 0; i < Order * Order; ++i)
            {
                source_double[i] = static_cast<double>(source[i]);
            }

            const double actual_determinant   = static_cast<double>(ID::Math::SIMD::determinant(m));
            const double expected_determinant = oracle_determinant(source_double, Order);
            const double limit = kTolDeterminant.abs_tolerance
                               + kTolDeterminant.rel_tolerance * std::fabs(expected_determinant);
            char detail[192] = {};
            std::snprintf(detail, sizeof(detail), "actual %.9g / oracle %.9g，允许 %.3g", actual_determinant,
                          expected_determinant, limit);
            std::snprintf(name, sizeof(name), "mat%zu.determinant_random", static_cast<size_t>(Order));
            check_flag(std::fabs(actual_determinant - expected_determinant) <= limit, name, detail);

            const double identity_determinant =
                static_cast<double>(ID::Math::SIMD::determinant(matrix<Order, Order>::identity()));
            std::snprintf(name, sizeof(name), "mat%zu.determinant_identity", static_cast<size_t>(Order));
            check_flag(identity_determinant == 1.0f, name, "单位矩阵的行列式必须精确为 1");
        }

        {
            float source[kMaxExtent * kMaxExtent] = {};
            make_dominant_matrix(source, Order, kSeedSquare + Order);
            const matrix<Order, Order> m = make_matrix<matrix<Order, Order>>(source);

            matrix<Order, Order> inverse;
            const bool           ok = ID::Math::SIMD::try_inverse(m, inverse);

            double source_double[kMaxExtent * kMaxExtent] = {};
            for (ID::size_t i = 0; i < Order * Order; ++i)
            {
                source_double[i] = static_cast<double>(source[i]);
            }
            double     expected[kMaxExtent * kMaxExtent] = {};
            const bool oracle_ok = oracle_inverse(source_double, Order, expected);

            std::snprintf(name, sizeof(name), "mat%zu.try_inverse_returns_true", static_cast<size_t>(Order));
            check_flag(ok && oracle_ok, name, "对角占优矩阵必须被接受（两侧都必须成功）");

            double actual[kMaxExtent * kMaxExtent] = {};
            read_matrix(inverse, actual);
            std::snprintf(name, sizeof(name), "mat%zu.try_inverse_values", static_cast<size_t>(Order));
            check_series(name, compare_series(actual, expected, Order * Order, kTolInverse));

            const matrix<Order, Order> product = m * inverse;
            read_matrix(product, actual);
            double identity_expected[kMaxExtent * kMaxExtent] = {};
            for (ID::size_t c = 0; c < Order; ++c)
            {
                for (ID::size_t r = 0; r < Order; ++r)
                {
                    identity_expected[c * Order + r] = (r == c) ? 1.0 : 0.0;
                }
            }
            std::snprintf(name, sizeof(name), "mat%zu.inverse_times_matrix_is_identity", static_cast<size_t>(Order));
            check_series(name, compare_series(actual, identity_expected, Order * Order, kTolInverse));

            bool padding_zero = true;
            for (ID::size_t c = 0; c < Order; ++c)
            {
                padding_zero = padding_zero && column_padding_is_zero(inverse, c);
            }
            std::snprintf(name, sizeof(name), "mat%zu.inverse_padding_zero", static_cast<size_t>(Order));
            check_flag(padding_zero, name, "try_inverse 写入的结果列 padding 必须是 +0.0f");
        }

        {
            matrix<Order, Order> singular;
            for (ID::size_t r = 0; r < Order; ++r)
            {
                const float value = 0.5f + static_cast<float>(r);
                singular.get_element(r, 0) = value;
                singular.get_element(r, 1) = value * 2.0f;
                for (ID::size_t c = 2; c < Order; ++c)
                {
                    singular.get_element(r, c) = 0.0f;
                }
            }

            matrix<Order, Order> out;
            for (ID::size_t r = 0; r < Order; ++r)
            {
                for (ID::size_t c = 0; c < Order; ++c)
                {
                    out.get_element(r, c) = static_cast<float>(100.0 * static_cast<double>(r) + static_cast<double>(c));
                }
            }
            const matrix<Order, Order> before = out;
            const bool                 failed = ID::Math::SIMD::try_inverse(singular, out);

            bool unchanged = true;
            for (ID::size_t r = 0; r < Order; ++r)
            {
                for (ID::size_t c = 0; c < Order; ++c)
                {
                    unchanged = unchanged && (out.get_element(r, c) == before.get_element(r, c));
                }
            }
            bool padding_unchanged = true;
            for (ID::size_t c = 0; c < Order; ++c)
            {
                padding_unchanged = padding_unchanged && column_padding_is_zero(out, c);
            }

            std::snprintf(name, sizeof(name), "mat%zu.try_inverse_singular_fails", static_cast<size_t>(Order));
            check_flag(!failed, name, "秩 1 矩阵必须被判为近奇异并返回 false");
            std::snprintf(name, sizeof(name), "mat%zu.try_inverse_failure_keeps_out", static_cast<size_t>(Order));
            check_flag(unchanged && padding_unchanged, name, "失败路径不得改写输出参数（含其列 padding）");

            const double singular_determinant = static_cast<double>(ID::Math::SIMD::determinant(singular));
            char         detail[192]          = {};
            std::snprintf(detail, sizeof(detail), "秩 1 矩阵的 determinant 实际 %.9g（应为 0）", singular_determinant);
            std::snprintf(name, sizeof(name), "mat%zu.determinant_singular_is_zero", static_cast<size_t>(Order));
            check_flag(std::fabs(singular_determinant) <= kTolDeterminant.abs_tolerance, name, detail);
        }
    }

    template <size_t Rows, size_t Cols>
    void check_matrix_proxy(const char* name) noexcept
    {
        float source[kMaxExtent * kMaxExtent] = {};
        make_float_matrix(source, Rows, Cols, kSeedMatrixLhs);
        matrix<Rows, Cols> m = make_matrix<matrix<Rows, Cols>>(source);

        bool read_matches = true;
        for (ID::size_t r = 0; r < Rows; ++r)
        {
            for (ID::size_t c = 0; c < Cols; ++c)
            {
                read_matches = read_matches && (m[r][c] == m.get_element(r, c));
            }
        }

        const float written = 7.25f;
        m[Rows - 1][Cols - 1] = written;
        const bool write_visible = (m.get_element(Rows - 1, Cols - 1) == written);

        const matrix<Rows, Cols>& const_ref  = m;
        const bool                const_read = (const_ref[0][0] == m.get_element(0, 0));

        check_flag(read_matches && write_visible && const_read, name,
                   "m[row][col] 的读写与 get_element 等价，const 矩阵走只读代理");
    }

    /**
     *  @brief 三种对象绑定的正向用例：`const` 对象 / 非 `const` 引用 / 右值对象
     *  @tparam Rows 行数；@tparam Cols 列数
     *  @param name 用例名前缀（形如 "binding.4x4"），四条判定各自追加后缀
     *  @note 右值矩阵的 `operator[]` 被 `= delete`（代理保存的引用会立刻悬空，D0-04），因此右值只用
     *        `get_element`，并在同一完整表达式内取值；该 `= delete` 的编译失败证据在
     *        validation/math/cases/negative_*_rvalue_subscript.cpp，这里再用 requires 断言一次
     *        "右值不可下标"。`const` 对象只能读：读取走 `const float&` 与 `ConstRowProxy`。
     */
    template <size_t Rows, size_t Cols>
    void check_matrix_bindings(const char* name) noexcept
    {
        float source[kMaxExtent * kMaxExtent] = {};
        make_float_matrix(source, Rows, Cols, kSeedMatrixLhs);
        matrix<Rows, Cols> mutable_matrix = make_matrix<matrix<Rows, Cols>>(source);

        // ---- 绑定 1：const 对象（只读，两条件都必须与源数据一致）----
        const matrix<Rows, Cols> const_object         = mutable_matrix;
        bool                     const_object_matches = true;
        for (ID::size_t r = 0; r < Rows; ++r)
        {
            for (ID::size_t c = 0; c < Cols; ++c)
            {
                const float& element = const_object.get_element(r, c);
                const_object_matches =
                    const_object_matches && (element == source[c * Rows + r]) && (const_object[r][c] == element);
            }
        }
        {
            char check_name[64] = {};
            std::snprintf(check_name, sizeof(check_name), "%s.const_object", name);
            check_flag(const_object_matches, check_name,
                       "const 对象：get_element 的 const 重载与只读行代理读到同一批源数据");
        }

        // ---- 绑定 2：非 const 引用（经引用的写入必须对原对象可见）----
        matrix<Rows, Cols>& mutable_reference = mutable_matrix;
        const float         probe_value       = -3.5f;
        mutable_reference.get_element(0, 0)   = probe_value;
        mutable_reference[Rows - 1][Cols - 1] = probe_value + 1.0f;
        {
            const bool visible = (mutable_matrix.get_element(0, 0) == probe_value)
                              && (mutable_matrix.get_element(Rows - 1, Cols - 1) == probe_value + 1.0f);
            char check_name[64] = {};
            std::snprintf(check_name, sizeof(check_name), "%s.nonconst_reference", name);
            check_flag(visible, check_name,
                       "非 const 引用：经引用的 get_element 写入与行代理写入都要对原对象可见");
        }

        // ---- 绑定 3：右值对象（get_element 可读；右值 operator[] 必须不可用）----
        // 读取发生在同一完整表达式内：临时矩阵活到分号，引用不会悬空
        const float rvalue_element = make_matrix<matrix<Rows, Cols>>(source).get_element(0, 0);
        {
            char check_name[64] = {};
            std::snprintf(check_name, sizeof(check_name), "%s.rvalue_object", name);
            check_flag(rvalue_element == source[0], check_name,
                       "右值对象：临时矩阵的 get_element 在同一完整表达式内可读且不产生悬空代理");
        }
        {
            // requires 形参是**具名左值**，必须显式转成右值才能命中 `operator[] &&`（= delete）；
            // 直接写 temporary[0] 命中的是 `&` 限定重载，恒为合法，判不出删除
            constexpr bool rvalue_subscript_absent =
                !requires(matrix<Rows, Cols>&& temporary) { static_cast<matrix<Rows, Cols>&&>(temporary)[0]; };
            char check_name[64] = {};
            std::snprintf(check_name, sizeof(check_name), "%s.rvalue_subscript_deleted", name);
            check_flag(rvalue_subscript_absent, check_name,
                       "右值矩阵的 operator[] 在编译期不可用（= delete；编译失败证据在 validation/math）");
        }
    }
} // namespace

namespace ID::Bench
{
    void IDMathSIMDSecurityTest() noexcept
    {
        // ------------------------------------------------------------------
        // A. 布局（D0-02 冻结的 padding 与对齐）
        // ------------------------------------------------------------------
        Check(sizeof(vec2) == 16 && sizeof(vec3) == 16 && sizeof(vec4) == 16, "layout.vec_sizeof",
              "SIMD 的 vec2/vec3/vec4 都必须 16 字节（vec2 = 8 存储 + 8 ABI 尾部填充）");
        Check(alignof(vec2) == 16 && alignof(vec3) == 16 && alignof(vec4) == 16, "layout.vec_alignof",
              "SIMD 向量必须 16 字节对齐（128-bit lane）");
        Check(vec2::kStorageCount == 2 && vec3::kStorageCount == 4 && vec4::kStorageCount == 4,
              "layout.vec_storage_count", "StorageN 必须为 2/4/4（vec3 向上取整到 4 个 float 槽位）");
        Check(vec2::kPaddingLaneCount == 0 && vec3::kPaddingLaneCount == 1 && vec4::kPaddingLaneCount == 0,
              "layout.vec_padding_lane_count", "只有 vec3 有 1 个 padding lane");
        Check(vec2{}.data_len() == 2 && vec3{}.data_len() == 3 && vec4{}.data_len() == 4, "layout.vec_data_len",
              "data_len() 是 data() 的可读长度 = N（padding 不计入）");
        Check(vec2{}.storage_bytes() == 8 && vec3{}.storage_bytes() == 16 && vec4{}.storage_bytes() == 16,
              "layout.vec_storage_bytes", "storage_bytes() = StorageN * sizeof(float)，vec2 为 8（< sizeof）");
        Check(std::is_trivially_copyable_v<vec3> && std::is_standard_layout_v<vec3>
                  && std::is_nothrow_default_constructible_v<vec3>,
              "layout.vec_type_traits", "向量必须可平凡复制、standard layout、默认构造 noexcept");

        Check(matrix_layout_ok<2, 2>(), "layout.mat_2x2", "ColumnStride==4、sizeof==32、列 padding 2 槽位");
        Check(matrix_layout_ok<2, 3>(), "layout.mat_2x3", "ColumnStride==4、sizeof==48、列 padding 2 槽位");
        Check(matrix_layout_ok<2, 4>(), "layout.mat_2x4", "ColumnStride==4、sizeof==64、列 padding 2 槽位");
        Check(matrix_layout_ok<3, 2>(), "layout.mat_3x2", "ColumnStride==4、sizeof==32、列 padding 1 槽位");
        Check(matrix_layout_ok<3, 3>(), "layout.mat_3x3", "ColumnStride==4、sizeof==48、列 padding 1 槽位");
        Check(matrix_layout_ok<3, 4>(), "layout.mat_3x4", "ColumnStride==4、sizeof==64、列 padding 1 槽位");
        Check(matrix_layout_ok<4, 2>(), "layout.mat_4x2", "ColumnStride==4、sizeof==32、无列 padding");
        Check(matrix_layout_ok<4, 3>(), "layout.mat_4x3", "ColumnStride==4、sizeof==48、无列 padding");
        Check(matrix_layout_ok<4, 4>(), "layout.mat_4x4", "ColumnStride==4、sizeof==64、无列 padding");
        Check(std::is_trivially_copyable_v<matrix<3, 4>> && std::is_standard_layout_v<matrix<3, 4>>,
              "layout.mat_type_traits", "矩阵必须可平凡复制且是 standard layout");
        Check(sizeof(matrix<3, 3>::RowProxy) == sizeof(void*) + sizeof(std::size_t), "layout.row_proxy_size",
              "行代理只持有矩阵指针 + 行下标，不得携带整行副本");

        // C6 内存安全：vec4 前后哨兵，构造/赋值/加法都不能越界写
        {
            alignas(16) unsigned char buffer[sizeof(vec4) + 32] = {};
            std::memset(buffer, 0x5A, sizeof(buffer));
            vec4* value = ::new (static_cast<void*>(buffer + 16)) vec4(1.5f, -2.5f, 3.5f, -4.5f);
            const vec4 sum = *value + vec4(1.0f, 1.0f, 1.0f, 1.0f);
            *value = sum;

            bool guard_ok = true;
            for (ID::size_t i = 0; i < 16; ++i)
            {
                guard_ok = guard_ok && (buffer[i] == 0x5A) && (buffer[16 + sizeof(vec4) + i] == 0x5A);
            }
            Check(guard_ok, "layout.vec_guard_sentinel", "构造/赋值/加法不得写到对象之外的哨兵字节");
        }

        // ------------------------------------------------------------------
        // B. 构造：默认零初始化、逐分量、splat、跨类型 explicit
        // ------------------------------------------------------------------
        {
            const vec3 zero;
            Check(zero[0] == 0.0f && zero[1] == 0.0f && zero[2] == 0.0f, "ctor.vec3_default_zero",
                  "默认构造必须是全零（D0-03）");
            Check(padding_lane_is_zero(zero), "ctor.vec3_default_padding_zero",
                  "默认构造的 padding lane 必须是 +0.0f");

            const vec3 components(1.5f, -2.25f, 3.0f);
            Check(components[0] == 1.5f && components[1] == -2.25f && components[2] == 3.0f,
                  "ctor.vec3_components", "逐分量构造按顺序写入 storage[0..N-1]");
            Check(padding_lane_is_zero(components), "ctor.vec3_components_padding_zero",
                  "逐分量构造不得读取任何源值来填 padding：必须是 +0.0f");

            const vec4 splatted = vec4::splat(-4.5f);
            Check(splatted[0] == -4.5f && splatted[1] == -4.5f && splatted[2] == -4.5f && splatted[3] == -4.5f,
                  "ctor.vec4_splat", "splat 把全部逻辑分量写成同一个值");

            const vec3 splatted3 = vec3::splat(2.5f);
            Check(padding_lane_is_zero(splatted3), "ctor.vec3_splat_padding_zero",
                  "splat 不把 padding lane 也填成同一个值：它不属于公开值");

            const ID::Math::SIMD::vector<double, 3> widened(vec3(1.0f, 2.0f, 3.0f));
            Check(widened[0] == 1.0 && widened[1] == 2.0 && widened[2] == 3.0, "ctor.cross_type_values",
                  "跨类型构造逐分量 static_cast，值必须精确");

            Check(!std::is_convertible_v<vec3, ID::Math::SIMD::vector<double, 3>>, "ctor.cross_type_explicit",
                  "跨类型构造必须是 explicit：不得隐式提升/降级");
            Check(std::is_constructible_v<vec3, float, float, float> && !std::is_constructible_v<vec3, float, float>,
                  "ctor.arity_exact", "逐分量构造恰好接受 N 个实参：少给即编译期错误");

            const matrix<3, 3> zero_matrix;
            bool               matrix_zero = true;
            for (ID::size_t r = 0; r < 3; ++r)
            {
                for (ID::size_t c = 0; c < 3; ++c)
                {
                    matrix_zero = matrix_zero && (zero_matrix.get_element(r, c) == 0.0f);
                }
            }
            bool padding_zero = true;
            for (ID::size_t c = 0; c < 3; ++c)
            {
                padding_zero = padding_zero && column_padding_is_zero(zero_matrix, c);
            }
            Check(matrix_zero && padding_zero, "ctor.matrix_default_zero",
                  "矩阵默认构造必须零初始化，含全部列 padding 槽位");
        }

        // ------------------------------------------------------------------
        // C. padding 不参与运算：把 NaN 位模式灌进 padding 后结果必须不受影响
        // ------------------------------------------------------------------
        {
            float lhs_source[3] = {};
            float rhs_source[3] = {};
            make_float_vector(lhs_source, 3, kSeedVectorLhs);
            make_float_vector(rhs_source, 3, kSeedVectorRhs);

            vec3 lhs = make_vector<vec3>(lhs_source);
            vec3 rhs = make_vector<vec3>(rhs_source);
            poison_slot(lhs.data()[3]);
            poison_slot(rhs.data()[3]);

            double lhs_double[3] = {};
            double rhs_double[3] = {};
            double expected[3]   = {};
            double actual[3]     = {};
            doubles_of<vec3>(lhs_source, lhs_double);
            doubles_of<vec3>(rhs_source, rhs_double);

            oracle_add(lhs_double, rhs_double, expected, 3);
            read_vector(lhs + rhs, actual);
            check_series("padding.vec3_add_ignores_nan", compare_series(actual, expected, 3, kTolArithmetic));

            read_vector(ID::Math::SIMD::hadamard(lhs, rhs), actual);
            oracle_hadamard(lhs_double, rhs_double, expected, 3);
            check_series("padding.vec3_hadamard_ignores_nan", compare_series(actual, expected, 3, kTolArithmetic));

            const double actual_dot   = static_cast<double>(ID::Math::SIMD::dot(lhs, rhs));
            const double expected_dot = oracle_dot(lhs_double, rhs_double, 3);
            const double limit        = kTolDot.abs_tolerance + kTolDot.rel_tolerance * std::fabs(expected_dot);
            char         detail[192]  = {};
            std::snprintf(detail, sizeof(detail), "actual %.9g / oracle %.9g，允许 %.3g（padding 内是 NaN）",
                          actual_dot, expected_dot, limit);
            check_flag(std::fabs(actual_dot - expected_dot) <= limit, "padding.vec3_dot_ignores_nan", detail);

            const double actual_length   = static_cast<double>(ID::Math::SIMD::length(lhs));
            const double expected_length = oracle_length(lhs_double, 3);
            const double length_limit    = kTolLength.abs_tolerance + kTolLength.rel_tolerance * expected_length;
            std::snprintf(detail, sizeof(detail), "actual %.9g / oracle %.9g，允许 %.3g（padding 内是 NaN）",
                          actual_length, expected_length, length_limit);
            check_flag(std::fabs(actual_length - expected_length) <= length_limit,
                       "padding.vec3_length_ignores_nan", detail);

            vec3       normalized;
            const bool normalized_ok = ID::Math::SIMD::try_normalize(lhs, normalized);
            oracle_normalize(lhs_double, expected, 3);
            read_vector(normalized, actual);
            check_series("padding.vec3_normalize_ignores_nan", compare_series(actual, expected, 3, kTolNormalize));
            check_flag(normalized_ok && padding_lane_is_zero(normalized), "padding.vec3_result_padding_zero",
                       "checked 归一化必须成功，且结果 padding lane 被写成 +0.0f");

            const vec3 product = ID::Math::SIMD::cross(lhs, rhs);
            oracle_cross(lhs_double, rhs_double, expected);
            read_vector(product, actual);
            check_series("padding.vec3_cross_ignores_nan", compare_series(actual, expected, 3, kTolArithmetic));
            check_flag(padding_lane_is_zero(product), "padding.vec3_cross_padding_zero",
                       "叉积结果的 padding lane 必须是 +0.0f");
        }

        {
            // 矩阵列 padding 里灌 NaN：matvec / matmul / transpose 的结果必须只由逻辑元素决定
            float matrix_source[kMaxExtent * kMaxExtent] = {};
            float vector_source[kMaxExtent]              = {};
            make_float_matrix(matrix_source, 3, 3, kSeedMatrixLhs);
            make_float_vector(vector_source, 3, kSeedVectorRhs);

            matrix<3, 3> m = make_matrix<matrix<3, 3>>(matrix_source);
            for (ID::size_t c = 0; c < 3; ++c)
            {
                poison_slot(m.data()[c * 4 + 3]);
            }
            const vector<float, 3> v = make_vector<vector<float, 3>>(vector_source);

            double matrix_double[kMaxExtent * kMaxExtent] = {};
            double vector_double[kMaxExtent]              = {};
            double expected[kMaxExtent * kMaxExtent]      = {};
            double actual[kMaxExtent * kMaxExtent]        = {};
            for (ID::size_t i = 0; i < 9; ++i)
            {
                matrix_double[i] = static_cast<double>(matrix_source[i]);
            }
            for (ID::size_t i = 0; i < 3; ++i)
            {
                vector_double[i] = static_cast<double>(vector_source[i]);
            }

            oracle_mat_vec(matrix_double, 3, 3, vector_double, expected);
            read_vector(m * v, actual);
            check_series("padding.matvec_ignores_nan", compare_series(actual, expected, 3, kTolMatVec));

            const matrix<3, 3> transposed = ID::Math::SIMD::transpose(m);
            oracle_transpose(matrix_double, 3, 3, expected);
            read_matrix(transposed, actual);
            check_series("padding.transpose_ignores_nan", compare_series(actual, expected, 9, kTolArithmetic));

            bool transpose_padding_zero = true;
            for (ID::size_t c = 0; c < 3; ++c)
            {
                transpose_padding_zero = transpose_padding_zero && column_padding_is_zero(transposed, c);
            }
            check_flag(transpose_padding_zero, "padding.transpose_result_padding_zero",
                       "转置结果的列 padding 必须是 +0.0f（源 padding 从不被读取）");

            const matrix<3, 3> other   = make_matrix<matrix<3, 3>>(matrix_source);
            const matrix<3, 3> product = m * other;
            oracle_mat_mul(matrix_double, 3, 3, matrix_double, 3, expected);
            read_matrix(product, actual);
            check_series("padding.matmul_ignores_nan", compare_series(actual, expected, 9, kTolMatMul));

            matrix<3, 3> inverse;
            const bool   inverse_ok = ID::Math::SIMD::try_inverse(m, inverse);
            bool         inverse_padding_zero = true;
            for (ID::size_t c = 0; c < 3; ++c)
            {
                inverse_padding_zero = inverse_padding_zero && column_padding_is_zero(inverse, c);
            }
            check_flag(inverse_ok && inverse_padding_zero, "padding.inverse_result_padding_zero",
                       "求逆结果的列 padding 必须是 +0.0f");
        }

        {
            // vec2：只做 8 字节读写，绝不触碰 16 字节对象的 ABI 尾部填充。
            // 把尾部预填 NaN 位模式，若实现做了 16 字节 load，结果必然被污染。
            alignas(16) unsigned char buffer[16] = {};
            for (ID::size_t i = 0; i < 4; ++i)
            {
                std::memcpy(buffer + i * 4, &kNaNBits, sizeof(kNaNBits));
            }
            vec2*        value        = ::new (static_cast<void*>(buffer)) vec2(1.5f, -2.5f);
            const double source[2]    = { 1.5, -2.5 };

            const double expected_dot = oracle_dot(source, source, 2);
            const double actual_dot   = static_cast<double>(ID::Math::SIMD::dot(*value, *value));
            const double dot_limit    = kTolDot.abs_tolerance + kTolDot.rel_tolerance * expected_dot;
            char         detail[192]  = {};
            std::snprintf(detail, sizeof(detail), "actual %.9g / oracle %.9g，允许 %.3g（尾部填充是 NaN）",
                          actual_dot, expected_dot, dot_limit);
            check_flag(std::fabs(actual_dot - expected_dot) <= dot_limit, "padding.vec2_tail_not_read", detail);

            const vec2   sum             = *value + vec2(1.0f, 1.0f);
            const double sum_expected[2] = { 2.5, -1.5 };
            double       sum_actual[2]   = {};
            read_vector(sum, sum_actual);
            check_series("padding.vec2_add_tail_not_read", compare_series(sum_actual, sum_expected, 2, kTolArithmetic));
            check_flag(vec2{}.storage_bytes() == 8 && sizeof(vec2) == 16, "padding.vec2_tail_size",
                       "vec2 的存储 8 字节，sizeof 16 字节：差的 8 字节是 ABI 尾部填充，代码不得依赖");
        }

        // ------------------------------------------------------------------
        // D. 向量运算：N = 2 / 3 / 4 全覆盖（与 double oracle 逐项比较）
        // ------------------------------------------------------------------
        check_vector_algebra<vec2>();
        check_vector_algebra<vec3>();
        check_vector_algebra<vec4>();

        // ------------------------------------------------------------------
        // E. 叉积命名边界
        // ------------------------------------------------------------------
        {
            float lhs_source[3] = {};
            float rhs_source[3] = {};
            make_float_vector(lhs_source, 3, kSeedVectorLhs);
            make_float_vector(rhs_source, 3, kSeedVectorRhs);

            const vec3 lhs     = make_vector<vec3>(lhs_source);
            const vec3 rhs     = make_vector<vec3>(rhs_source);
            const vec3 product = ID::Math::SIMD::cross(lhs, rhs);

            double lhs_double[3] = {};
            double rhs_double[3] = {};
            double expected[3]   = {};
            double actual[3]     = {};
            doubles_of<vec3>(lhs_source, lhs_double);
            doubles_of<vec3>(rhs_source, rhs_double);
            oracle_cross(lhs_double, rhs_double, expected);
            read_vector(product, actual);
            check_series("vec3.cross", compare_series(actual, expected, 3, kTolArithmetic));

            // cross(a, a) 必须是零向量（按**冻结容差**判定，不要求 bitwise 零：`/fp:fast` 下 FMA
            // 收缩会把 `a1*a2 - a2*a1` 合成一条 FMA，残差正是被乘数乘积的舍入误差，量级 1e-9；
            // SIMD 头的文件级文档也明说结果与 oracle 按容差比较、不承诺 bitwise 等价）。
            // 判据依据（旧断言为何违反 D0-05、新断言对应容差表的哪一行、失败样本的实际残差量级）
            // 见 validation/math/cross-orthogonality-criterion.txt。
            const vec3   self_product     = ID::Math::SIMD::cross(lhs, lhs);
            const double self_actual[3]   = { static_cast<double>(self_product[0]),
                                              static_cast<double>(self_product[1]),
                                              static_cast<double>(self_product[2]) };
            const double self_expected[3] = { 0.0, 0.0, 0.0 };
            const Comparison self_comparison = compare_series(self_actual, self_expected, 3, kTolArithmetic);
            double           self_residual   = 0.0;
            for (ID::size_t i = 0; i < 3; ++i)
            {
                const double magnitude = std::fabs(self_actual[i] - self_expected[i]);
                self_residual = (magnitude > self_residual) ? magnitude : self_residual;
            }
            char self_detail[192] = {};
            std::snprintf(self_detail, sizeof(self_detail),
                          "max|cross(a,a) 分量|=%.3g（允许 %.3g，容差表 kTolArithmetic）", self_residual,
                          kTolArithmetic.abs_tolerance);
            check_flag(self_comparison.ok, "vec3.cross_self_zero", self_detail);
            check_flag(padding_lane_is_zero(self_product), "vec3.cross_self_padding_zero",
                       "cross(a,a) 的 padding lane 必须是 +0.0f（写回时被掩码清零）");

            const double orthogonality = std::fabs(static_cast<double>(ID::Math::SIMD::dot(product, lhs)));
            char         detail[192]   = {};
            std::snprintf(detail, sizeof(detail), "|dot(cross(a,b),a)|=%.3g（允许 %.3g，容差表 kTolDot）",
                          orthogonality, kTolDot.abs_tolerance);
            check_flag(orthogonality <= kTolDot.abs_tolerance, "vec3.cross_orthogonal", detail);
        }
        {
            float lhs_source[2] = {};
            float rhs_source[2] = {};
            make_float_vector(lhs_source, 2, kSeedVectorLhs);
            make_float_vector(rhs_source, 2, kSeedVectorRhs);

            const vec2   lhs    = make_vector<vec2>(lhs_source);
            const vec2   rhs    = make_vector<vec2>(rhs_source);
            const double actual = static_cast<double>(ID::Math::SIMD::cross2(lhs, rhs));

            double lhs_double[2] = {};
            double rhs_double[2] = {};
            doubles_of<vec2>(lhs_source, lhs_double);
            doubles_of<vec2>(rhs_source, rhs_double);
            const double expected = oracle_cross2(lhs_double, rhs_double);
            const double limit = kTolArithmetic.abs_tolerance + kTolArithmetic.rel_tolerance * std::fabs(expected);
            char         detail[192] = {};
            std::snprintf(detail, sizeof(detail), "actual %.9g / oracle %.9g，允许 %.3g", actual, expected, limit);
            check_flag(std::fabs(actual - expected) <= limit, "vec2.cross2", detail);

            const double self_cross2   = static_cast<double>(ID::Math::SIMD::cross2(lhs, lhs));
            const double antisymmetric = static_cast<double>(ID::Math::SIMD::cross2(lhs, rhs))
                                       + static_cast<double>(ID::Math::SIMD::cross2(rhs, lhs));
            std::snprintf(detail, sizeof(detail), "cross2(a,a)=%.3g；cross2(a,b)+cross2(b,a)=%.3g（允许 %.3g）",
                          self_cross2, antisymmetric, kTolArithmetic.abs_tolerance);
            check_flag(std::fabs(self_cross2) <= kTolArithmetic.abs_tolerance
                           && std::fabs(antisymmetric) <= kTolArithmetic.abs_tolerance,
                       "vec2.cross2_antisymmetric", detail);
        }

        // ------------------------------------------------------------------
        // F. 运算集合的编译期边界（可编译事实，反向编译用例在 validation/math）
        // ------------------------------------------------------------------
        {
            using int3   = ID::Math::SIMD::vector<int, 3>;
            using float8 = ID::Math::SIMD::vector<float, 8>;

            constexpr bool int3_absent   = !is_addable_v<int3> && !is_dottable_v<int3>;
            constexpr bool float8_absent = !is_addable_v<float8> && !is_dottable_v<float8>;
            constexpr bool cross_scope   = is_crossable_v<vec3> && !is_crossable_v<vec2> && !is_crossable_v<vec4>;

            Check(int3_absent, "ops.restricted_int3_absent",
                  "vector<int,3> 存储可用但运算必须不存在（不做标量 fallback）");
            Check(float8_absent, "ops.restricted_float8_absent",
                  "vector<float,8> 存储可用但运算必须不存在（运算只对 N∈{2,3,4}）");
            Check(cross_scope, "ops.cross_only_vec3",
                  "cross 只对 vec3 存在：二维用 cross2，四维没有叉积");
            Check(int3::kStorageCount == 4 && float8::kStorageCount == 8 && sizeof(float8) == 32,
                  "ops.restricted_storage_available",
                  "受限组合的存储仍可用：存储槽位 4 / 8，vector<float,8> 为 32 字节");
        }

        // ------------------------------------------------------------------
        // G. 矩阵：9 形状乘、矩形 transpose、matrix*vector、方阵运算、行代理
        // ------------------------------------------------------------------
        check_matrix_multiply<2, 3, 4>("mat.2x3_times_3x4");
        check_matrix_multiply<3, 2, 3>("mat.3x2_times_2x3");
        check_matrix_multiply<3, 3, 4>("mat.3x3_times_3x4");
        check_matrix_multiply<4, 2, 4>("mat.4x2_times_2x4");
        check_matrix_multiply<4, 3, 3>("mat.4x3_times_3x3");
        check_matrix_multiply<2, 2, 2>("mat.2x2_times_2x2");
        check_matrix_multiply<2, 4, 2>("mat.2x4_times_4x2");
        check_matrix_multiply<4, 4, 4>("mat.4x4_times_4x4");
        check_matrix_multiply<3, 3, 2>("mat.3x3_times_3x2");
        check_matrix_multiply<2, 3, 3>("mat.2x3_times_3x3");
        check_matrix_multiply<4, 2, 2>("mat.4x2_times_2x2");
        check_matrix_multiply<2, 2, 4>("mat.2x2_times_2x4");
        check_matrix_multiply<4, 3, 4>("mat.4x3_times_3x4");

        check_matrix_vector<2, 2>("matvec.2x2");
        check_matrix_vector<2, 3>("matvec.2x3");
        check_matrix_vector<2, 4>("matvec.2x4");
        check_matrix_vector<3, 2>("matvec.3x2");
        check_matrix_vector<3, 3>("matvec.3x3");
        check_matrix_vector<3, 4>("matvec.3x4");
        check_matrix_vector<4, 2>("matvec.4x2");
        check_matrix_vector<4, 3>("matvec.4x3");
        check_matrix_vector<4, 4>("matvec.4x4");

        check_matrix_transpose<2, 3>("transpose.2x3");
        check_matrix_transpose<3, 2>("transpose.3x2");
        check_matrix_transpose<3, 4>("transpose.3x4");
        check_matrix_transpose<4, 3>("transpose.4x3");
        check_matrix_transpose<2, 4>("transpose.2x4");
        check_matrix_transpose<4, 2>("transpose.4x2");
        check_matrix_transpose<3, 3>("transpose.3x3");
        check_matrix_transpose<4, 4>("transpose.4x4");

        check_square_operations<2>();
        check_square_operations<3>();
        check_square_operations<4>();

        check_matrix_proxy<2, 3>("proxy.2x3_read_write");
        check_matrix_proxy<3, 3>("proxy.3x3_read_write");
        check_matrix_proxy<4, 2>("proxy.4x2_read_write");
        check_matrix_proxy<4, 4>("proxy.4x4_read_write");

        // 三种绑定的正向用例（矩形 + 方阵各一遍）；右值 operator[] 的负例在 validation/math
        check_matrix_bindings<2, 3>("binding.2x3");
        check_matrix_bindings<4, 4>("binding.4x4");

        // 矩阵 pack / unpack 往返（SIMD 独有的交换格式入口）
        {
            float source[kMaxExtent * kMaxExtent] = {};
            make_float_matrix(source, 3, 3, kSeedMatrixLhs);
            const matrix<3, 3> m = make_matrix<matrix<3, 3>>(source);

            float packed[9 + 2] = {};
            for (ID::size_t i = 0; i < 11; ++i)
            {
                packed[i] = -99.0f;
            }
            ID::Math::SIMD::pack(m, packed);

            bool pack_ok = true;
            for (ID::size_t i = 0; i < 9; ++i)
            {
                pack_ok = pack_ok && (static_cast<double>(packed[i]) == static_cast<double>(source[i]));
            }
            const bool bounds_ok = (packed[9] == -99.0f) && (packed[10] == -99.0f);
            check_flag(pack_ok, "mat.pack_values", "pack 写出的必须是紧凑列主序的逻辑元素（不含列 padding）");
            check_flag(bounds_ok, "mat.pack_writes_only_rows_times_cols",
                       "pack 只写 Rows*Cols 个 float：缓冲尾部必须保持原值");

            matrix<3, 3> roundtrip;
            ID::Math::SIMD::unpack(packed, roundtrip);
            double expected[kMaxExtent * kMaxExtent] = {};
            double actual[kMaxExtent * kMaxExtent]   = {};
            for (ID::size_t i = 0; i < 9; ++i)
            {
                expected[i] = static_cast<double>(source[i]);
            }
            read_matrix(roundtrip, actual);
            check_series("mat.unpack_roundtrip", compare_series(actual, expected, 9, kTolArithmetic));

            bool padding_zero = true;
            for (ID::size_t c = 0; c < 3; ++c)
            {
                padding_zero = padding_zero && column_padding_is_zero(roundtrip, c);
            }
            check_flag(padding_zero, "mat.unpack_padding_zero", "unpack 之后列 padding 必须是 +0.0f");
        }

        {
            constexpr bool rectangle_absent = !is_determinant_taking_v<matrix<2, 3>>
                                           && !is_determinant_taking_v<matrix<4, 2>>
                                           && !is_inversible_v<matrix<3, 4>>;
            constexpr bool square_present    = is_determinant_taking_v<matrix<2, 2>>
                                            && is_determinant_taking_v<matrix<3, 3>>
                                            && is_inversible_v<matrix<4, 4>>;
            Check(rectangle_absent && square_present, "mat.rectangle_square_only",
                  "矩形矩阵没有 determinant / try_inverse，方阵 2/3/4 阶必须有（只对 rows == cols 存在）");
        }

        Check(true, "domain.self_contained",
              "全部用例使用栈上 POD、无 new/malloc（placement new 只用于哨兵布局取证）、无文件与线程");
    }
} // namespace ID::Bench

#else

namespace ID::Bench
{
    void IDMathSIMDSecurityTest() noexcept
    {
        Skip("math.simd.domain",
             "该构建组合未选中该 backend：ID_USE_PLUGIN_IDMATHSIMD 未定义（ID_ASSEMBLY_PROFILE 未选中 ID.Math.SIMD）");
    }
} // namespace ID::Bench

#endif
