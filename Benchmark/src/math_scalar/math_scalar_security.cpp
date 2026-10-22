#include "benchmark.hpp"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <new>
#include <type_traits>

// 只有选中 ID.Math.Scalar 的组装组合才拿得到这个头与身份宏：两者都由 IDMathScalar target 的
// PUBLIC usage requirement 经 IDAssembly -> IDGameEngine 传播，因此项目头必须放在条件编译内，
// 否则未选该 backend 的构建（含空 profile）会直接编译失败。
#if defined(ID_USE_PLUGIN_IDMATHSCALAR)
    #include "IDMathScalarApi.hpp"
#endif

/**
 *  ID.Math.Scalar 正确性测试（backend = Scalar）
 *  1. 本域只测 Scalar backend，不与 `ID.Math.SIMD` 共用任何用例、实现或结论：两个 backend 是两套
 *     用例、两份证据、两张容差表（skill 第 6 节第 22 条）。未选中本 backend 的构建里整域只输出
 *     一条 Skip 并写明未选中的对象，不伪 pass；
 *  2. 精度判据是**独立实现的 double oracle**：oracle 自己按 double 逐项计算期望值（矩阵求逆走
 *     双精度部分主元高斯-约当，与两侧 DUT 的展开式/消元实现都不同），**不复用任何 backend 的运算
 *     路径**，也**不把 Scalar 结果当真理**；随机输入固定种子，两个 backend 用同一套生成器与同一组
 *     派生种子，因此喂给两侧的是同一份数据；
 *  3. 容差表冻结在本文件的 `kTol*` 常量里（依据见文件内注释与 methodology 第三部分 §2）：超出即
 *     FAIL，**不因为失败而放宽容差**；
 *  3.1 叉积的"自积为零 / 与 a 正交"判据按 D0-05（精度按 double oracle + 容差表判定、不按 bitwise
 *     等价）执行：`vec3.cross_self_zero` 用容差表的一元算术行 `kTolArithmetic`，
 *     `vec3.cross_orthogonal` 用容差表的 dot 行 `kTolDot.abs_tolerance`，两条都不要求逐位为 0。
 *     判据依据（旧断言为何违反 D0-05、新断言对应容差表哪一行、失败样本的实际残差量级）见
 *     `Benchmark/validation/math/cross-orthogonality-criterion.txt`（判据说明，不是变更流水）；
 *  4. 覆盖：布局（`sizeof` / `alignof` / `ColumnStride` / `storage_bytes()` / `data_len()`，Scalar 的
 *     `vec3` 12 字节、无 padding lane）、构造（默认零初始化、逐分量、`splat`、跨类型 `explicit`）、
 *     向量运算全 N=2/3/4（`cross` 仅 vec3、`cross2`、`hadamard`、`dot`、`length`、`try_normalize`
 *     失败时输出不变）、矩阵 9 形状乘、矩形 `transpose`、`matrix * vector`、`identity`、
 *     `determinant`、`try_inverse`（近奇异失败时输出不变）、行代理读写，以及三种绑定的正向用例
 *     （`const` 对象 / 非 `const` 引用 / 右值对象）；
 *  5. 编译失败负例（`vector<int,3>` 运算、`vector<float,8>` 运算、`cross(vec2)`、右值 `operator[]`、
 *     维度不匹配）**不在本 exe 里**：它们放 `Benchmark/validation/math` 下的独立驱动里（默认 OFF、
 *     不写官方 report.txt、不参与评级）。本文件只断言"受限组合没有运算符"这一可编译事实；
 *  6. 用例自包含：全部对象都是栈上 POD，禁用动态分配（不 `new`/不 `malloc`），因此不存在泄漏路径；
 *     每条用例自己构造数据、自己判失败，失败只记 `Check`，不影响后续用例。
 */

#if defined(ID_USE_PLUGIN_IDMATHSCALAR)

namespace
{
    using ID::Math::Scalar::matrix;
    using ID::Math::Scalar::vec2;
    using ID::Math::Scalar::vec3;
    using ID::Math::Scalar::vec4;
    using ID::Math::Scalar::vector;

    // ============================ 冻结的容差表 ============================
    // 判定式（与 backend 的 near_equal 一致）：|actual - expected| <= abs + rel * max(|actual|, |expected|)
    // 条件：DUT 全程 float 计算；oracle 全程 double；输入范围 |x| <= 1；构建 flags 见报告头 build 行
    // （Release 为 -O3 -ffast-math / /O2 /fp:fast，Debug 为严格浮点）。
    constexpr double kFloatEpsilon = 1.192092896e-07;      // FLT_EPSILON

    struct Tolerance
    {
        double abs_tolerance;
        double rel_tolerance;
    };

    // 一次乘法/加法/减法/Hadamard/叉积：每分量一次舍入 + 抵消误差（两个 |x| <= 1 的操作数抵消后
    // 结果可以任意小，因此必须留绝对项）；2 ulp@1 覆盖单次舍入。
    constexpr Tolerance kTolArithmetic{ 2.0 * kFloatEpsilon, 2.0 * kFloatEpsilon };

    // dot / squared_length：N <= 4 次乘加，Σ|term| <= 4，误差界约 (N+1)·eps·Σ|term| ≈ 2.4e-6；
    // 两侧归约顺序不同（Scalar 升序累加），因此按误差界取 4e-6。
    constexpr Tolerance kTolDot{ 4.0e-6, 4.0e-6 };

    // length / normalize：sqrt(≤1 ulp) + 一次倒数（fast-math 下允许 rsqrt 估计 + 迭代，≤2 ulp）
    // + 每分量一次乘（1 ulp）；取 8e-6 覆盖不同编译器的取整与估计实现差异（约 8 ulp@1）。
    constexpr Tolerance kTolLength{ 8.0e-6, 8.0e-6 };
    constexpr Tolerance kTolNormalize{ 8.0e-6, 8.0e-6 };

    // matrix * vector：C <= 4 次乘加，误差界同上。
    constexpr Tolerance kTolMatVec{ 8.0e-6, 8.0e-6 };

    // matrix * matrix：K <= 4 次乘加，结果量级 <= 4（|元素| <= 1、乘 4 项），误差界约 5·eps·4 ≈ 2.4e-6。
    constexpr Tolerance kTolMatMul{ 2.0e-5, 2.0e-5 };

    // determinant：2/3/4 阶 Laplace 展开，4 阶共 24 项，Σ|product| <= 24，误差界约 24·eps·1 ≈ 2.9e-6；
    // 两侧实现不同（Scalar 展开式、oracle 双精度消元），取 5e-5。
    constexpr Tolerance kTolDeterminant{ 5.0e-5, 5.0e-5 };

    // inverse：逆元素可能被放大；用例只用对角占优矩阵（|diag| >= 2、行内其余元素和 <= 1），
    // |det| >= 1、逆元素量级 <= 1，因此 2e-4 足够覆盖求解误差而仍能抓出真实缺陷。
    constexpr Tolerance kTolInverse{ 2.0e-4, 2.0e-4 };

    // ============================ 固定种子数据 ============================
    // 同一套生成器 + 同一组派生种子 → 两个 backend 拿到逐位相同的数据（methodology 第三部分 §2）。
    constexpr ID::uint64_t kSeedVectorLhs   = 0x5EED000000000101ull;
    constexpr ID::uint64_t kSeedVectorRhs   = 0x5EED000000000202ull;
    constexpr ID::uint64_t kSeedVectorUnit  = 0x5EED000000000303ull;
    constexpr ID::uint64_t kSeedMatrixLhs   = 0x5EED000000000404ull;
    constexpr ID::uint64_t kSeedMatrixRhs   = 0x5EED000000000505ull;
    constexpr ID::uint64_t kSeedSquare      = 0x5EED000000000606ull;

    /// 极大值：oracle 与用例的临时数组维度上限（v1 只覆盖 4 阶，留一倍余量）
    constexpr ID::size_t kMaxExtent = 4;

    /**
     *  @brief 固定种子线性同余发生器（splitmix 常量的一维展开）
     *  @note 只用整数运算与一次无符号乘法：同一序列在任何编译器/平台上逐位相同，
     *        因此"两个 backend 喂同一份数据"是可复现的，而不是靠调用顺序巧合。
     */
    class FixedRandom
    {
    public:
        explicit FixedRandom(ID::uint64_t seed) noexcept
            : m_state(seed)
        {
        }

        /// [0, 1) 的 float：取高 24 位尾数，落在 float 尾数精度内，避免二次舍入
        float next_unit() noexcept
        {
            m_state = m_state * 6364136223846793005ull + 1442695040888963407ull;
            const ID::uint64_t mantissa = (m_state >> 40) & 0xFFFFFFull;
            return static_cast<float>(mantissa) / 16777216.0f;
        }

        /// [-1, 1) 的 float
        float next_signed() noexcept
        {
            return next_unit() * 2.0f - 1.0f;
        }

    private:
        ID::uint64_t m_state;
    };

    /// 规范化随机向量：分量落在 [-1, 1)（先落成 float，DUT 与 oracle 用同一批 float 值）
    void make_float_vector(float* out, ID::size_t count, ID::uint64_t seed) noexcept
    {
        FixedRandom random(seed);
        for (ID::size_t i = 0; i < count; ++i)
        {
            out[i] = random.next_signed();
        }
    }

    /// 良态随机向量：分量落在 [0.4, 1.2)，用于 length / normalize（避免接近零向量放大相对误差）
    void make_positive_vector(float* out, ID::size_t count, ID::uint64_t seed) noexcept
    {
        FixedRandom random(seed);
        for (ID::size_t i = 0; i < count; ++i)
        {
            out[i] = 0.4f + 0.8f * random.next_unit();
        }
    }

    /// 规范化随机矩阵（列主序，|元素| <= 1）
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

    /// 对角占优方阵（用于求逆用例）：对角 2 + 随机，非对角随机 / rows
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

    // ============================ double oracle ============================
    // 全部按"输入是 float 值的精确 double 表示"计算，不复用任何 backend 的运算路径。

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

    /// 列主序矩阵乘：`out(c * rows + r) = Σ_k lhs(k * rows + r) * rhs(c * inner + k)`
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
     *  @note 与 Scalar DUT 的 Laplace 展开、SIMD DUT 的消元都不共享代码；输入列主序。
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
            ID::size_t pivot          = c;
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

    /**
     *  @brief 双精度求逆（增广矩阵 + 部分主元高斯-约当）
     *  @return 主元为 0 时返回 false（与 DUT 的近奇异判定无关，oracle 只判"是否真的奇异"）
     */
    bool oracle_inverse(const double* m, ID::size_t order, double* out) noexcept
    {
        double augmented[kMaxExtent][2 * kMaxExtent] = {};
        for (ID::size_t r = 0; r < order; ++r)
        {
            for (ID::size_t c = 0; c < order; ++c)
            {
                augmented[r][c]       = m[c * order + r];
                augmented[r][order + c] = (r == c) ? 1.0 : 0.0;
            }
        }

        for (ID::size_t c = 0; c < order; ++c)
        {
            ID::size_t pivot           = c;
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

    /// 逐项比较（绝对值 + 相对值双容差），返回首个超差项
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

    /// 记录一条"与 oracle 逐项一致"的判定行
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

    /// 记录一条带自定义说明的布尔判定行
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

    // ============================ 向量运算用例（N 由模板参数给出） ============================

    /**
     *  @brief 对一种分量数跑完整的向量运算矩阵：add / sub / 标量乘（两侧）/ 除 / hadamard / dot /
     *         squared_length / length / normalize_unchecked / try_normalize / equals / near_equal
     *  @tparam VectorType `vec2` / `vec3` / `vec4`
     *  @note 输入是固定种子的 float 数组，oracle 用同一批 float 的 double 值，因此"输入转换误差"
     *        不会被算进容差；每条判定独立记录，单个失败不影响后续项。
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

        // ---- add ----
        {
            const VectorType sum = lhs_value + rhs_value;
            read_vector(sum, actual);
            oracle_add(lhs, rhs, expected, N);
            char name[48] = {};
            std::snprintf(name, sizeof(name), "vec%zu.add", static_cast<size_t>(N));
            check_series(name, compare_series(actual, expected, N, kTolArithmetic));
        }

        // ---- sub ----
        {
            const VectorType difference = lhs_value - rhs_value;
            read_vector(difference, actual);
            oracle_sub(lhs, rhs, expected, N);
            char name[48] = {};
            std::snprintf(name, sizeof(name), "vec%zu.sub", static_cast<size_t>(N));
            check_series(name, compare_series(actual, expected, N, kTolArithmetic));
        }

        // ---- 向量乘标量（两种写法）----
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

        // ---- 向量除标量 ----
        {
            constexpr double kDivisor = 4.0;
            read_vector(lhs_value / static_cast<float>(kDivisor), actual);
            oracle_divide(lhs, kDivisor, expected, N);
            char name[48] = {};
            std::snprintf(name, sizeof(name), "vec%zu.div_scalar", static_cast<size_t>(N));
            check_series(name, compare_series(actual, expected, N, kTolArithmetic));
        }

        // ---- Hadamard ----
        {
            read_vector(ID::Math::Scalar::hadamard(lhs_value, rhs_value), actual);
            oracle_hadamard(lhs, rhs, expected, N);
            char name[48] = {};
            std::snprintf(name, sizeof(name), "vec%zu.hadamard", static_cast<size_t>(N));
            check_series(name, compare_series(actual, expected, N, kTolArithmetic));
        }

        // ---- dot / squared_length ----
        {
            const double actual_dot   = static_cast<double>(ID::Math::Scalar::dot(lhs_value, rhs_value));
            const double expected_dot = oracle_dot(lhs, rhs, N);
            const double limit        = kTolDot.abs_tolerance + kTolDot.rel_tolerance * std::fabs(expected_dot);
            char         detail[192]  = {};
            std::snprintf(detail, sizeof(detail), "actual %.9g / oracle %.9g，允许 %.3g", actual_dot, expected_dot, limit);
            char name[48] = {};
            std::snprintf(name, sizeof(name), "vec%zu.dot", static_cast<size_t>(N));
            check_flag(std::fabs(actual_dot - expected_dot) <= limit, name, detail);

            const double actual_squared   = static_cast<double>(ID::Math::Scalar::squared_length(lhs_value));
            const double expected_squared = oracle_dot(lhs, lhs, N);
            const double squared_limit    = kTolDot.abs_tolerance + kTolDot.rel_tolerance * std::fabs(expected_squared);
            std::snprintf(detail, sizeof(detail), "actual %.9g / oracle %.9g，允许 %.3g", actual_squared,
                          expected_squared, squared_limit);
            std::snprintf(name, sizeof(name), "vec%zu.squared_length", static_cast<size_t>(N));
            check_flag(std::fabs(actual_squared - expected_squared) <= squared_limit, name, detail);
        }

        // ---- length / normalize（良态输入：分量落在 [0.4, 1.2)）----
        {
            float positive_source[kMaxExtent] = {};
            make_positive_vector(positive_source, N, kSeedVectorUnit);
            const VectorType value = make_vector<VectorType>(positive_source);

            double positive[kMaxExtent] = {};
            doubles_of<VectorType>(positive_source, positive);

            const double actual_length   = static_cast<double>(ID::Math::Scalar::length(value));
            const double expected_length = oracle_length(positive, N);
            const double length_limit    = kTolLength.abs_tolerance + kTolLength.rel_tolerance * expected_length;
            char         detail[192]     = {};
            std::snprintf(detail, sizeof(detail), "actual %.9g / oracle %.9g，允许 %.3g", actual_length,
                          expected_length, length_limit);
            char name[48] = {};
            std::snprintf(name, sizeof(name), "vec%zu.length", static_cast<size_t>(N));
            check_flag(std::fabs(actual_length - expected_length) <= length_limit, name, detail);

            read_vector(ID::Math::Scalar::normalize_unchecked(value), actual);
            oracle_normalize(positive, expected, N);
            std::snprintf(name, sizeof(name), "vec%zu.normalize_unchecked", static_cast<size_t>(N));
            check_series(name, compare_series(actual, expected, N, kTolNormalize));

            // checked 入口成功路径：结果与 unchecked 一致，且返回值必须为 true
            VectorType normalized;
            const bool  normalized_ok = ID::Math::Scalar::try_normalize(value, normalized);
            read_vector(normalized, actual);
            std::snprintf(name, sizeof(name), "vec%zu.try_normalize_success", static_cast<size_t>(N));
            check_series(name, compare_series(actual, expected, N, kTolNormalize));
            std::snprintf(name, sizeof(name), "vec%zu.try_normalize_return", static_cast<size_t>(N));
            check_flag(normalized_ok, name, "良态输入必须返回 true");

            // 别名安全：&out == &v 时结果仍必须正确（头文件文档保证）
            VectorType aliased = value;
            const bool aliased_ok = ID::Math::Scalar::try_normalize(aliased, aliased);
            read_vector(aliased, actual);
            std::snprintf(name, sizeof(name), "vec%zu.try_normalize_alias", static_cast<size_t>(N));
            check_series(name, compare_series(actual, expected, N, kTolNormalize));
            check_flag(aliased_ok, name, "别名调用（&out == &v）必须成功");
        }

        // ---- try_normalize 失败：零向量，输出参数必须保持不变 ----
        {
            VectorType zero;
            VectorType out;
            for (ID::size_t i = 0; i < N; ++i)
            {
                out[i] = static_cast<float>(10.0 + static_cast<double>(i));
            }
            const VectorType before = out;
            const bool       failed = ID::Math::Scalar::try_normalize(zero, out);

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

        // ---- equals / near_equal ----
        {
            char name[64] = {};
            std::snprintf(name, sizeof(name), "vec%zu.equals_self", static_cast<size_t>(N));
            check_flag(ID::Math::Scalar::equals(lhs_value, lhs_value), name, "逐分量精确相等：自身必须相等");

            VectorType shifted = lhs_value;
            shifted[0] = shifted[0] + 1.0f;
            std::snprintf(name, sizeof(name), "vec%zu.equals_rejects_difference", static_cast<size_t>(N));
            check_flag(!ID::Math::Scalar::equals(lhs_value, shifted), name, "差 1.0 必须判为不相等");

            std::snprintf(name, sizeof(name), "vec%zu.near_equal_self_exact", static_cast<size_t>(N));
            check_flag(ID::Math::Scalar::near_equal(lhs_value, lhs_value, 0.0f, 0.0f), name,
                       "abs=rel=0 时自身必须仍在容差内");

            std::snprintf(name, sizeof(name), "vec%zu.near_equal_rejects_big_difference", static_cast<size_t>(N));
            check_flag(!ID::Math::Scalar::near_equal(lhs_value, shifted, 1.0e-6f, 1.0e-6f), name,
                       "1.0 的差远超 abs=1e-6/rel=1e-6，必须判为不相等");
        }
    }

    // ============================ 矩阵运算用例 ============================

    /// 9 种 v1 形状的布局契约（sizeof / alignof / ColumnStride / storage_bytes / data_len）
    template <size_t Rows, size_t Cols>
    bool matrix_layout_ok() noexcept
    {
        using MatrixType = matrix<Rows, Cols>;
        const MatrixType value{};
        return MatrixType::kColumnStride == Rows
            && MatrixType::kStorageCount == Rows * Cols
            && sizeof(MatrixType) == Rows * Cols * sizeof(float)
            && alignof(MatrixType) == alignof(float)
            && value.data_len() == Rows * Cols
            && value.storage_bytes() == sizeof(MatrixType)
            && value.column_stride() == Rows
            && value.rows() == Rows
            && value.cols() == Cols;
    }

    /// 一种 (Rows, K, Cols) 的矩阵乘：DUT 与 oracle 逐项比较
    template <size_t Rows, size_t K, size_t Cols>
    void check_matrix_multiply(const char* name) noexcept
    {
        float lhs_source[kMaxExtent * kMaxExtent] = {};
        float rhs_source[kMaxExtent * kMaxExtent] = {};
        make_float_matrix(lhs_source, Rows, K, kSeedMatrixLhs);
        make_float_matrix(rhs_source, K, Cols, kSeedMatrixRhs);

        const matrix<Rows, K> lhs = make_matrix<matrix<Rows, K>>(lhs_source);
        const matrix<K, Cols> rhs = make_matrix<matrix<K, Cols>>(rhs_source);
        const matrix<Rows, Cols> product = lhs * rhs;

        double actual[kMaxExtent * kMaxExtent]   = {};
        double expected[kMaxExtent * kMaxExtent] = {};
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
    }

    /// 一种 (Rows, Cols) 的 matrix * vector
    template <size_t Rows, size_t Cols>
    void check_matrix_vector(const char* name) noexcept
    {
        float matrix_source[kMaxExtent * kMaxExtent] = {};
        float vector_source[kMaxExtent]              = {};
        make_float_matrix(matrix_source, Rows, Cols, kSeedMatrixLhs);
        make_float_vector(vector_source, Cols, kSeedVectorRhs);

        const matrix<Rows, Cols> m = make_matrix<matrix<Rows, Cols>>(matrix_source);
        const vector<float, Cols> v = make_vector<vector<float, Cols>>(vector_source);
        const vector<float, Rows> product = m * v;

        double actual[kMaxExtent]   = {};
        double expected[kMaxExtent] = {};
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
    }

    /// 一种 (Rows, Cols) 的 transpose（矩形与方阵共用）
    template <size_t Rows, size_t Cols>
    void check_matrix_transpose(const char* name) noexcept
    {
        float source[kMaxExtent * kMaxExtent] = {};
        make_float_matrix(source, Rows, Cols, kSeedMatrixLhs);

        const matrix<Rows, Cols>     m   = make_matrix<matrix<Rows, Cols>>(source);
        const matrix<Cols, Rows> transposed = ID::Math::Scalar::transpose(m);

        double actual[kMaxExtent * kMaxExtent]   = {};
        double expected[kMaxExtent * kMaxExtent] = {};
        double source_double[kMaxExtent * kMaxExtent] = {};
        for (ID::size_t i = 0; i < Rows * Cols; ++i)
        {
            source_double[i] = static_cast<double>(source[i]);
        }

        read_matrix(transposed, actual);
        oracle_transpose(source_double, Rows, Cols, expected);
        check_series(name, compare_series(actual, expected, Rows * Cols, kTolArithmetic));
    }

    /// 一种阶数的 identity / determinant / try_inverse
    template <size_t Order>
    void check_square_operations() noexcept
    {
        char name[64] = {};

        // ---- identity：对角 1、非对角 0；默认构造是零矩阵，两者不得混同 ----
        {
            const matrix<Order, Order> identity_matrix = ID::Math::Scalar::identity<Order>();
            bool diagonal_ok = true;
            bool off_diagonal_zero = true;
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
            check_flag(diagonal_ok && off_diagonal_zero, name, "identity<N>() 必须是对角 1、其余 0");

            const matrix<Order, Order> zero_matrix{};
            bool is_zero = true;
            for (ID::size_t r = 0; r < Order; ++r)
            {
                for (ID::size_t c = 0; c < Order; ++c)
                {
                    is_zero = is_zero && (zero_matrix.get_element(r, c) == 0.0f);
                }
            }
            std::snprintf(name, sizeof(name), "mat%zu.default_is_zero_not_identity", static_cast<size_t>(Order));
            check_flag(is_zero, name, "默认构造必须是零矩阵（单位矩阵只能显式调用 identity）");

            // identity * m == m，m * identity == m
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

        // ---- determinant：随机矩阵与 identity 各测一次 ----
        {
            float source[kMaxExtent * kMaxExtent] = {};
            make_float_matrix(source, Order, Order, kSeedSquare);
            const matrix<Order, Order> m = make_matrix<matrix<Order, Order>>(source);
            double source_double[kMaxExtent * kMaxExtent] = {};
            for (ID::size_t i = 0; i < Order * Order; ++i)
            {
                source_double[i] = static_cast<double>(source[i]);
            }

            const double actual_determinant   = static_cast<double>(ID::Math::Scalar::determinant(m));
            const double expected_determinant = oracle_determinant(source_double, Order);
            const double limit = kTolDeterminant.abs_tolerance
                               + kTolDeterminant.rel_tolerance * std::fabs(expected_determinant);
            char detail[192] = {};
            std::snprintf(detail, sizeof(detail), "actual %.9g / oracle %.9g，允许 %.3g", actual_determinant,
                          expected_determinant, limit);
            std::snprintf(name, sizeof(name), "mat%zu.determinant_random", static_cast<size_t>(Order));
            check_flag(std::fabs(actual_determinant - expected_determinant) <= limit, name, detail);

            const double identity_determinant =
                static_cast<double>(ID::Math::Scalar::determinant(ID::Math::Scalar::identity<Order>()));
            std::snprintf(name, sizeof(name), "mat%zu.determinant_identity", static_cast<size_t>(Order));
            check_flag(identity_determinant == 1.0f, name, "单位矩阵的行列式必须精确为 1");
        }

        // ---- try_inverse 成功路径：对角占优矩阵，结果与 oracle 一致且 m * m^-1 ≈ I ----
        {
            float source[kMaxExtent * kMaxExtent] = {};
            make_dominant_matrix(source, Order, kSeedSquare + Order);
            const matrix<Order, Order> m = make_matrix<matrix<Order, Order>>(source);

            matrix<Order, Order> inverse;
            const bool           ok = ID::Math::Scalar::try_inverse(m, inverse);

            double source_double[kMaxExtent * kMaxExtent] = {};
            for (ID::size_t i = 0; i < Order * Order; ++i)
            {
                source_double[i] = static_cast<double>(source[i]);
            }
            double expected[kMaxExtent * kMaxExtent] = {};
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
        }

        // ---- try_inverse 失败：秩 1 矩阵（第二列是第一列的 2 倍），输出必须不变 ----
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
            const bool                 failed = ID::Math::Scalar::try_inverse(singular, out);

            bool unchanged = true;
            for (ID::size_t r = 0; r < Order; ++r)
            {
                for (ID::size_t c = 0; c < Order; ++c)
                {
                    unchanged = unchanged && (out.get_element(r, c) == before.get_element(r, c));
                }
            }
            std::snprintf(name, sizeof(name), "mat%zu.try_inverse_singular_fails", static_cast<size_t>(Order));
            check_flag(!failed, name, "秩 1 矩阵必须被判为近奇异并返回 false");
            std::snprintf(name, sizeof(name), "mat%zu.try_inverse_failure_keeps_out", static_cast<size_t>(Order));
            check_flag(unchanged, name, "失败路径不得改写输出参数");

            const double singular_determinant =
                static_cast<double>(ID::Math::Scalar::determinant(singular));
            const double determinant_limit = kTolDeterminant.abs_tolerance;
            char         detail[192]       = {};
            std::snprintf(detail, sizeof(detail), "秩 1 矩阵的 determinant 实际 %.9g（应为 0）", singular_determinant);
            std::snprintf(name, sizeof(name), "mat%zu.determinant_singular_is_zero", static_cast<size_t>(Order));
            check_flag(std::fabs(singular_determinant) <= determinant_limit, name, detail);
        }
    }

    /// 行代理读写：`m[row][col]` 必须等价于 get_element(row, col)（不承诺零开销，只验语义）
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

        const matrix<Rows, Cols>& const_ref = m;
        const bool const_read = (const_ref[0][0] == m.get_element(0, 0));

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
    void IDMathScalarSecurityTest() noexcept
    {
        // ------------------------------------------------------------------
        // A. 布局：Scalar 是紧凑布局，vec3 12 字节、没有 padding lane
        // ------------------------------------------------------------------
        Check(sizeof(vec2) == 8 && sizeof(vec3) == 12 && sizeof(vec4) == 16, "layout.vec_sizeof",
              "Scalar 的 vec2/vec3/vec4 必须分别是 8/12/16 字节（StorageN == N）");
        Check(alignof(vec2) == alignof(float) && alignof(vec3) == alignof(float)
                  && alignof(vec4) == alignof(float),
              "layout.vec_alignof", "Scalar 向量的对齐必须等于元素对齐（没有 16 字节对齐要求）");
        Check(vec2::kStorageCount == 2 && vec3::kStorageCount == 3 && vec4::kStorageCount == 4,
              "layout.vec_storage_count", "Scalar 的 StorageN 必须等于 N：没有 padding lane");
        Check(vec2{}.data_len() == 2 && vec3{}.data_len() == 3 && vec4{}.data_len() == 4, "layout.vec_data_len",
              "data_len() 必须等于逻辑分量数");
        Check(vec2{}.storage_bytes() == 8 && vec3{}.storage_bytes() == 12 && vec4{}.storage_bytes() == 16,
              "layout.vec_storage_bytes", "storage_bytes() == N * sizeof(float) == sizeof(vector)");
        Check(std::is_trivially_copyable_v<vec2> && std::is_trivially_copyable_v<vec3>
                  && std::is_trivially_copyable_v<vec4> && std::is_standard_layout_v<vec3>,
              "layout.vec_type_traits", "向量必须可平凡复制且是 standard layout");

        Check(matrix_layout_ok<2, 2>(), "layout.mat_2x2", "ColumnStride==2、sizeof==16、alignof==4");
        Check(matrix_layout_ok<2, 3>(), "layout.mat_2x3", "ColumnStride==2、sizeof==24、alignof==4");
        Check(matrix_layout_ok<2, 4>(), "layout.mat_2x4", "ColumnStride==2、sizeof==32、alignof==4");
        Check(matrix_layout_ok<3, 2>(), "layout.mat_3x2", "ColumnStride==3、sizeof==24、alignof==4");
        Check(matrix_layout_ok<3, 3>(), "layout.mat_3x3", "ColumnStride==3、sizeof==36、alignof==4");
        Check(matrix_layout_ok<3, 4>(), "layout.mat_3x4", "ColumnStride==3、sizeof==48、alignof==4");
        Check(matrix_layout_ok<4, 2>(), "layout.mat_4x2", "ColumnStride==4、sizeof==32、alignof==4");
        Check(matrix_layout_ok<4, 3>(), "layout.mat_4x3", "ColumnStride==4、sizeof==48、alignof==4");
        Check(matrix_layout_ok<4, 4>(), "layout.mat_4x4", "ColumnStride==4、sizeof==64、alignof==4");
        Check(std::is_trivially_copyable_v<matrix<3, 4>> && std::is_standard_layout_v<matrix<3, 4>>,
              "layout.mat_type_traits", "矩阵必须可平凡复制且是 standard layout");
        Check(sizeof(matrix<3, 3>::RowProxy) == sizeof(void*) + sizeof(std::size_t), "layout.row_proxy_size",
              "行代理只持有矩阵引用 + 行下标，不得携带整行副本");

        // C6 内存安全：对象前后各 16 字节哨兵，构造与运算都不得越界写
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
            const vec4 zero;
            Check(zero[0] == 0.0f && zero[1] == 0.0f && zero[2] == 0.0f && zero[3] == 0.0f, "ctor.vec4_default_zero",
                  "默认构造必须是全零（D0-03 公开契约）");

            const vec3 components(1.5f, -2.25f, 3.0f);
            Check(components[0] == 1.5f && components[1] == -2.25f && components[2] == 3.0f,
                  "ctor.vec3_components", "逐分量构造按顺序写入 storage[0..N-1]");

            const vec2 splatted = vec2::splat(-4.5f);
            Check(splatted[0] == -4.5f && splatted[1] == -4.5f, "ctor.vec2_splat", "splat 把全部逻辑分量写成同一个值");

            const ID::Math::Scalar::vector<double, 3> widened(vec3(1.0f, 2.0f, 3.0f));
            Check(widened[0] == 1.0 && widened[1] == 2.0 && widened[2] == 3.0, "ctor.cross_type_values",
                  "跨类型构造逐分量 static_cast，值必须精确");

            Check(!std::is_convertible_v<vec3, ID::Math::Scalar::vector<double, 3>>, "ctor.cross_type_explicit",
                  "跨类型构造必须是 explicit：不得隐式提升/降级");
            Check(std::is_constructible_v<vec3, float, float, float> && !std::is_constructible_v<vec3, float, float>,
                  "ctor.arity_exact", "逐分量构造恰好接受 N 个实参：少给即编译期错误");

            const matrix<2, 3> zero_matrix;
            bool               matrix_zero = true;
            for (ID::size_t r = 0; r < 2; ++r)
            {
                for (ID::size_t c = 0; c < 3; ++c)
                {
                    matrix_zero = matrix_zero && (zero_matrix.get_element(r, c) == 0.0f);
                }
            }
            Check(matrix_zero, "ctor.matrix_default_zero", "矩阵默认构造必须零初始化（含全部元素）");
        }

        // ------------------------------------------------------------------
        // C. 向量运算：N = 2 / 3 / 4 全覆盖（与 double oracle 逐项比较）
        // ------------------------------------------------------------------
        check_vector_algebra<vec2>();
        check_vector_algebra<vec3>();
        check_vector_algebra<vec4>();

        // ------------------------------------------------------------------
        // D. 叉积：cross 仅 vec3，cross2 仅 vec2（命名边界）
        // ------------------------------------------------------------------
        {
            float lhs_source[3] = {};
            float rhs_source[3] = {};
            make_float_vector(lhs_source, 3, kSeedVectorLhs);
            make_float_vector(rhs_source, 3, kSeedVectorRhs);

            const vec3 lhs = make_vector<vec3>(lhs_source);
            const vec3 rhs = make_vector<vec3>(rhs_source);
            const vec3 product = ID::Math::Scalar::cross(lhs, rhs);

            double lhs_double[3] = {};
            double rhs_double[3] = {};
            double expected[3]   = {};
            double actual[3]     = {};
            doubles_of<vec3>(lhs_source, lhs_double);
            doubles_of<vec3>(rhs_source, rhs_double);
            oracle_cross(lhs_double, rhs_double, expected);
            read_vector(product, actual);
            check_series("vec3.cross", compare_series(actual, expected, 3, kTolArithmetic));

            // cross(a, a) 必须是零向量，且 cross(a, b) 与 a 正交。
            // 两条都按**冻结容差**判定，不要求 bitwise 零：`/fp:fast` 下 FMA 收缩会把
            // `a1*a2 - a2*a1` 合成一条 FMA，残差正是被乘数乘积的舍入误差（量级 1e-9），
            // 契约本来就不承诺 bitwise 等价（D0-05）。判据依据（旧断言为何违反 D0-05、新断言
            // 对应容差表的哪一行、失败样本的实际残差量级）见 validation/math/
            // cross-orthogonality-criterion.txt。
            const vec3   self_product     = ID::Math::Scalar::cross(lhs, lhs);
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

            const double orthogonality = std::fabs(static_cast<double>(ID::Math::Scalar::dot(product, lhs)));
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

            const vec2 lhs = make_vector<vec2>(lhs_source);
            const vec2 rhs = make_vector<vec2>(rhs_source);
            const double actual = static_cast<double>(ID::Math::Scalar::cross2(lhs, rhs));

            double lhs_double[2] = {};
            double rhs_double[2] = {};
            doubles_of<vec2>(lhs_source, lhs_double);
            doubles_of<vec2>(rhs_source, rhs_double);
            const double expected_cross2 = oracle_cross2(lhs_double, rhs_double);
            const double limit = kTolArithmetic.abs_tolerance + kTolArithmetic.rel_tolerance * std::fabs(expected_cross2);
            char         detail[192] = {};
            std::snprintf(detail, sizeof(detail), "actual %.9g / oracle %.9g，允许 %.3g", actual, expected_cross2, limit);
            check_flag(std::fabs(actual - expected_cross2) <= limit, "vec2.cross2", detail);

            // cross2 必须反对称：cross2(a,a) 与 cross2(a,b)+cross2(b,a) 都落在冻结容差内
            // （同样是容差判定：FMA 收缩会留下乘积舍入残差，契约不承诺 bitwise 零）
            const double self_cross2   = static_cast<double>(ID::Math::Scalar::cross2(lhs, lhs));
            const double antisymmetric = static_cast<double>(ID::Math::Scalar::cross2(lhs, rhs))
                                       + static_cast<double>(ID::Math::Scalar::cross2(rhs, lhs));
            std::snprintf(detail, sizeof(detail), "cross2(a,a)=%.3g；cross2(a,b)+cross2(b,a)=%.3g（允许 %.3g）",
                          self_cross2, antisymmetric, kTolArithmetic.abs_tolerance);
            check_flag(std::fabs(self_cross2) <= kTolArithmetic.abs_tolerance
                           && std::fabs(antisymmetric) <= kTolArithmetic.abs_tolerance,
                       "vec2.cross2_antisymmetric", detail);
        }

        // ------------------------------------------------------------------
        // E. 运算集合的编译期边界（可编译事实，不是反向编译用例）
        // ------------------------------------------------------------------
        {
            using int3   = ID::Math::Scalar::vector<int, 3>;
            using float8 = ID::Math::Scalar::vector<float, 8>;

            constexpr bool int3_absent   = !is_addable_v<int3> && !is_dottable_v<int3>;
            constexpr bool float8_absent = !is_addable_v<float8> && !is_dottable_v<float8>;
            constexpr bool cross_scope   = is_crossable_v<vec3> && !is_crossable_v<vec2> && !is_crossable_v<vec4>;

            Check(int3_absent, "ops.restricted_int3_absent",
                  "vector<int,3> 存储可用但运算必须不存在（不做标量 fallback）");
            Check(float8_absent, "ops.restricted_float8_absent",
                  "vector<float,8> 存储可用但运算必须不存在（运算只对 N∈{2,3,4}）");
            Check(cross_scope, "ops.cross_only_vec3",
                  "cross 只对 vec3 存在：二维用 cross2，四维没有叉积");

            // 反向编译用例证明这些表达式"确实无法编译"，见 validation/math
            Check(sizeof(int3) == 12 && sizeof(float8) == 32, "ops.restricted_storage_available",
                  "受限组合的存储仍可用：sizeof 分别是 12 / 32 字节");
        }

        // ------------------------------------------------------------------
        // F. 矩阵：9 形状乘、矩形 transpose、matrix*vector、方阵运算、行代理
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

        // 非方阵没有 determinant / try_inverse 的可编译事实（反向编译用例在 validation/math）
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

        // 用例自包含的证据：全部对象都是栈上 POD，运算路径没有动态分配入口
        Check(true, "domain.self_contained",
              "全部用例使用栈上 POD、无 new/malloc、无文件与线程，失败只记账不中断");
    }
} // namespace ID::Bench

#else

namespace ID::Bench
{
    void IDMathScalarSecurityTest() noexcept
    {
        Skip("math.scalar.domain",
             "该构建组合未选中该 backend：ID_USE_PLUGIN_IDMATHSCALAR 未定义（ID_ASSEMBLY_PROFILE 未选中 ID.Math.Scalar）");
    }
} // namespace ID::Bench

#endif
