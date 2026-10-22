#include "benchmark.hpp"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string_view>

// 与 Security 同口径：公开头与身份宏只在选中本 backend 的组装组合里可见，因此必须条件包含。
#if defined(ID_USE_PLUGIN_IDMATHSCALAR)
    #include "IDMathScalarApi.hpp"
#endif

/**
 *  ID.Math.Scalar 性能基准（backend = Scalar）
 *  1. 指标集合（12 条，覆盖 B03 要求的全部面）：`vec.add.latency` / `vec.add.throughput` /
 *     `vec.dot` / `vec.normalize` / `vec.add.aos` / `vec.add.soa` / `mat.matvec` / `mat.matmul` /
 *     `mat.access.const_index` / `mat.access.dynamic_index` / `mat.access.proxy_const_index` /
 *     `mat.access.proxy_dynamic_index`；
 *  2. **单链延迟与独立吞吐分开**：`vec.add.latency` 是单一 loop-carried 依赖链（每次迭代的一次向量加法
 *     依赖上一次结果）；`vec.add.throughput` 是 4 条互相独立的依赖链。两者不可互相代替；
 *  3. **AoS / SoA 分别测**：`vec.add.aos` 在"64 个 vec4 连续排列"上做 `dot`，`vec.add.soa` 在"4 个分量
 *     数组各 64 个 float"上先 gather 构造向量再做 `dot`；两条指标喂同一批数值，差异只来自布局；
 *  4. **参考值是 `regression`（本机同配置历史基线），不是同轮对照**：每条指标仍在同一轮、同一进程、
 *     相邻位置测一份本文件自己实现的等价手写对照（纯 float 数组 + 显式循环，不复用 DUT 的任何运算
 *     路径，且与 DUT 一样是 `ID_FORCEINLINE`），但**对照只作披露、不作参考值**——
 *     `vec.add.latency` / `mat.access.const_index` / `mat.access.proxy_const_index` 的 DUT 与对照在
 *     Release 下生成**逐字相同**的机器码（见 `build/math-both-mingw-release/machine-code-evidence.txt`
 *     的 3.1 / 3.3），此时"参考值 = 对照 × 1.0"必然退化成 ratio ≡ 1，评级不携带任何质量信息。
 *     参考值改取 `Benchmark/baseline/<cpu>-<compiler>-<buildtype>.txt` 的历史实测中位数（同机器、
 *     同编译器、同构建类型、同 flags、同输入数据、同测量协议），由编译期分支唯一选择，未知配置
 *     直接编译失败；ratio ≈ 1 只说明"相对该基线未退化"（回归门槛），不是绝对质量结论。
 *     查不到基线值的指标按 `ungraded` 输出并计 FAIL，**禁止**回退到同轮对照或其它配置的值；
 *  5. 测量协议：从 iters=1 倍增标定，直到单批 ≥ 100 ms 或撞上限（标定段同时完成预热），再跑一次满尺寸
 *     非计时预热；样本固定 5 轮取中位数，极差 > 20% 时在 note 里标 unstable。DUT 与对照在**每一轮内
 *     相邻**测量（先 DUT 后对照），共享同一批缓存与频率状态；
 *  6. 防优化：`KeepAlive` 在计时区间之外、每批一次；结果累加进 probe 的 `sink` 成员。访问器指标经
 *     `volatile` 限定指针读取：否则常量索引的 load 会被循环不变量提升，读数退化成空循环（DUT 与对照
 *     两侧都这样处理，比值仍然可比）；
 *  7. 与 `ID.Math.SIMD` 的读数**只能**在同机器、同配置、同 flags、同精度模式下比较（skill 第 6 节第
 *     25 条）；本域不宣称任何"SIMD 更快/更慢"的结论，两个 backend 各出各的证据；
 *  8. 未选中本 backend 的构建里整域只输出一条 Skip 并写明未选中的对象，不伪 pass、不出指标行。
 */

#if defined(ID_USE_PLUGIN_IDMATHSCALAR)

namespace
{
    using ID::Math::Scalar::matrix;
    using ID::Math::Scalar::vec4;

    // 单批目标时长：低于 100 ms 就等于把调度噪声当读数
    constexpr double     kTargetRoundNs       = 100.0e6;
    constexpr ID::size_t kCalibrateStartIters = 1;
    constexpr ID::size_t kMaxIters            = 1u << 30;
    constexpr int        kRounds              = 5;

    /// 向量数据元素个数：轮转周期取 64，远高于编译器展开上限，避免循环不变量被提升
    constexpr ID::size_t kElementCount = 64;

    /// 矩阵数据元素个数：两路轮转（8 × 8）得到 64 周期
    constexpr ID::size_t kMatrixCount = 8;

    /// 固定种子（与 Security 不同的派生种子，但同样确定性可复现）
    constexpr ID::uint64_t kSeedVectorA = 0x5EED0000000010A1ull;
    constexpr ID::uint64_t kSeedVectorB = 0x5EED0000000010B2ull;
    constexpr ID::uint64_t kSeedMatrixA = 0x5EED0000000010C3ull;
    constexpr ID::uint64_t kSeedMatrixB = 0x5EED0000000010D4ull;
    constexpr ID::uint64_t kSeedSoA     = 0x5EED0000000010E5ull;

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

    struct Sample
    {
        double     median_ns          = 0.0;     // 5 轮中位数（每 op），报告值
        double     min_ns             = 0.0;
        double     max_ns             = 0.0;
        double     spread             = 0.0;     // (max - min) / median，用于判定 unstable
        ID::size_t iters              = 0;       // 单批迭代数
        ID::size_t ops_per_iteration  = 1;       // 每次迭代包含的 op 个数
    };

    /// 一批 op 的整批经过时间（ns）；KeepAlive 在计时区间之外
    template <typename Op>
    double time_batch(Op& op, ID::size_t iters) noexcept
    {
        double elapsed = 0.0;
        {
            const ID::Bench::Timer timer;
            op(iters);
            elapsed = timer.ElapsedNs();
        }
        ID::Bench::KeepAlive(&op.sink);
        return elapsed;
    }

    /// 倍增标定单批迭代数（标定段同时是预热：缓存与分支预测逐级摸热）
    template <typename Op>
    ID::size_t calibrate(Op& op) noexcept
    {
        ID::size_t iters = kCalibrateStartIters;
        for (;;)
        {
            const double elapsed = time_batch(op, iters);
            if (elapsed >= kTargetRoundNs || iters >= kMaxIters)
            {
                return iters;
            }
            const ID::size_t next = iters * 2;
            iters = next > kMaxIters ? kMaxIters : next;
        }
    }

    void sort_samples(double* samples, int count) noexcept
    {
        for (int i = 1; i < count; ++i)
        {
            const double key = samples[i];
            int          j   = i - 1;
            while (j >= 0 && samples[j] > key)
            {
                samples[j + 1] = samples[j];
                --j;
            }
            samples[j + 1] = key;
        }
    }

    void summarize(double* samples, int count, ID::size_t iters, ID::size_t ops_per_iteration,
                   Sample& sample) noexcept
    {
        sort_samples(samples, count);
        sample.min_ns            = samples[0];
        sample.max_ns            = samples[count - 1];
        sample.median_ns         = samples[count / 2];
        sample.spread            = sample.median_ns > 0.0 ? (sample.max_ns - sample.min_ns) / sample.median_ns : 0.0;
        sample.iters             = iters;
        sample.ops_per_iteration = ops_per_iteration;
    }

    /**
     *  @brief 同轮测量一对 op：DUT 与对照在每一轮内相邻测量（同进程、同迭代数）
     *  @param dut 被测操作
     *  @param control 同语义的手写对照
     *  @param ops_per_iteration 每次迭代包含多少个 op，用于换算 ns/op
     *  @note 迭代数只由 DUT 标定；对照复用同一迭代数（对照更贵时单批更长，不影响每 op 换算，
     *        并且已在 note 里披露）
     */
    template <typename DutOp, typename ControlOp>
    void measure_pair(DutOp& dut, ControlOp& control, ID::size_t ops_per_iteration, Sample& dut_sample,
                      Sample& control_sample) noexcept
    {
        const ID::size_t iters = calibrate(dut);
        static_cast<void>(time_batch(dut, iters));        // 满尺寸非计时预热
        static_cast<void>(time_batch(control, iters));

        double dut_samples[kRounds]     = {};
        double control_samples[kRounds] = {};
        for (int round = 0; round < kRounds; ++round)
        {
            const double divisor   = static_cast<double>(iters * ops_per_iteration);
            dut_samples[round]     = time_batch(dut, iters) / divisor;
            control_samples[round] = time_batch(control, iters) / divisor;
        }

        summarize(dut_samples, kRounds, iters, ops_per_iteration, dut_sample);
        summarize(control_samples, kRounds, iters, ops_per_iteration, control_sample);
    }

    // ============================ 本机同配置历史基线（basis = regression） ============================
    // 参考值来源：`Benchmark/baseline/<cpu>-<compiler>-<buildtype>.txt` 里 `ID.Math.Scalar|<指标名>` 行
    // 的历史实测中位数（本机 13th Gen Intel Core i7-13700，同编译器、同构建类型、同 flags、同输入
    // 数据、同测量协议）。为什么不用同轮对照作参考值，见文件头第 4 条：部分指标的 DUT 与对照生成
    // 逐字相同的机器码，"参考值 = 对照 × 1.0"会退化成 ratio ≡ 1。

    struct BaselineEntry
    {
        std::string_view name;                 // 指标名，与 record() 的 name 逐字一致
        double           reference_ns = 0.0;   // 该配置下的历史实测中位数（ns/op）
    };

    // 每个 (编译器, 构建类型) 一套；未知组合必须编译失败，不得借用其它配置的值
    #if defined(_MSC_VER) && defined(NDEBUG)
        // 来源：Benchmark/baseline/i7-13700-msvc19.52-release.txt
        constexpr char          kBaselineFile[] = "i7-13700-msvc19.52-release.txt";
        constexpr BaselineEntry kBaseline[]     = {
            { "vec.add.latency.ns",                0.42 },
            { "vec.add.throughput.ns",             0.93 },
            { "vec.dot.ns",                        0.73 },
            { "vec.normalize.ns",                  3.11 },
            { "vec.add.aos.ns",                    0.73 },
            { "vec.add.soa.ns",                    1.57 },
            { "mat.matvec.ns",                     1.66 },
            { "mat.matmul.ns",                     8.85 },
            { "mat.access.const_index.ns",         0.41 },
            { "mat.access.dynamic_index.ns",       0.64 },
            { "mat.access.proxy_const_index.ns",   0.41 },
            { "mat.access.proxy_dynamic_index.ns", 0.64 },
        };
    #elif defined(_MSC_VER)
        // 来源：Benchmark/baseline/i7-13700-msvc19.52-debug.txt
        constexpr char          kBaselineFile[] = "i7-13700-msvc19.52-debug.txt";
        constexpr BaselineEntry kBaseline[]     = {
            { "vec.add.latency.ns",                26.91 },
            { "vec.add.throughput.ns",             24.41 },
            { "vec.dot.ns",                        11.47 },
            { "vec.normalize.ns",                  57.32 },
            { "vec.add.aos.ns",                    11.70 },
            { "vec.add.soa.ns",                    12.93 },
            { "mat.matvec.ns",                     79.47 },
            { "mat.matmul.ns",                    240.38 },
            { "mat.access.const_index.ns",         1.67 },
            { "mat.access.dynamic_index.ns",       1.65 },
            { "mat.access.proxy_const_index.ns",   4.01 },
            { "mat.access.proxy_dynamic_index.ns", 3.76 },
        };
    #elif defined(__GNUC__) && defined(NDEBUG)
        // 来源：Benchmark/baseline/i7-13700-gcc16.2-release.txt
        constexpr char          kBaselineFile[] = "i7-13700-gcc16.2-release.txt";
        constexpr BaselineEntry kBaseline[]     = {
            { "vec.add.latency.ns",                0.41 },
            { "vec.add.throughput.ns",             0.21 },
            { "vec.dot.ns",                        0.81 },
            { "vec.normalize.ns",                  0.99 },
            { "vec.add.aos.ns",                    0.81 },
            { "vec.add.soa.ns",                    0.55 },
            { "mat.matvec.ns",                     0.87 },
            { "mat.matmul.ns",                     3.08 },
            { "mat.access.const_index.ns",         0.41 },
            { "mat.access.dynamic_index.ns",       0.41 },
            { "mat.access.proxy_const_index.ns",   0.41 },
            { "mat.access.proxy_dynamic_index.ns", 0.41 },
        };
    #elif defined(__GNUC__)
        // 来源：Benchmark/baseline/i7-13700-gcc16.2-debug.txt
        constexpr char          kBaselineFile[] = "i7-13700-gcc16.2-debug.txt";
        constexpr BaselineEntry kBaseline[]     = {
            { "vec.add.latency.ns",                7.57 },
            { "vec.add.throughput.ns",             7.74 },
            { "vec.dot.ns",                        4.88 },
            { "vec.normalize.ns",                 20.54 },
            { "vec.add.aos.ns",                    4.82 },
            { "vec.add.soa.ns",                    8.68 },
            { "mat.matvec.ns",                    35.65 },
            { "mat.matmul.ns",                   147.40 },
            { "mat.access.const_index.ns",         1.65 },
            { "mat.access.dynamic_index.ns",       1.65 },
            { "mat.access.proxy_const_index.ns",   1.66 },
            { "mat.access.proxy_dynamic_index.ns", 1.94 },
        };
    #else
        #error "ID.Math.Scalar 的 regression 基线未覆盖当前配置：不得借用其它配置的参考值，必须先按本机实测补基线文件"
    #endif

    /**
     *  @brief 按指标名取本机同配置历史基线
     *  @param name 指标名，必须与基线文件里的名字逐字一致
     *  @return 基线 ns/op；查不到返回 0，由 RecordMetric 判为 ungraded 并计 FAIL
     *  @note 缺基线时绝不回退到同轮对照或其它配置：报告必须显式暴露"没有依据"，而不是静默换口径
     */
    double baseline_reference(std::string_view name) noexcept
    {
        for (const BaselineEntry& entry : kBaseline)
        {
            if (entry.name == name)
            {
                return entry.reference_ns;
            }
        }
        return 0.0;
    }

    /**
     *  @brief 记录一条指标：参考值 = 本机同配置历史基线，basis 固定 regression
     *  @param name 指标名，必须与基线文件里的名字逐字一致
     *  @param sample DUT 的 5 轮样本
     *  @param control_sample 同轮对照样本；**只作披露**，不作为参考值
     *  @param note 该指标的 DUT/对照说明（不含固定前缀，函数内统一补齐）
     *  @note 查不到基线值时 reference 传 0，报告输出 ungraded 并计 FAIL，note 里写明原因
     */
    void record(const char* name, const Sample& sample, const Sample& control_sample, const char* note) noexcept
    {
        const double reference_ns = baseline_reference(name);
        char         spread_text[96] = {};
        if (sample.spread > 0.20)
        {
            std::snprintf(spread_text, sizeof(spread_text), "；unstable: 5 轮极差 %.0f%%", sample.spread * 100.0);
        }
        const double batch_ms =
            sample.median_ns * static_cast<double>(sample.iters * sample.ops_per_iteration) / 1.0e6;

        // 缺基线时把"为什么 ungraded"写进报告，避免读报告的人误以为评级被静默跳过
        char ungraded_text[240] = {};
        if (!(reference_ns > 0.0))
        {
            std::snprintf(ungraded_text, sizeof(ungraded_text),
                          "；ungraded: baseline/%s 缺该指标行（不编造、不回退其它配置）",
                          kBaselineFile);
        }

        char buffer[1200] = {};
        std::snprintf(buffer, sizeof(buffer),
                      "%s%s；参考值 %.2f ns 取自 %s；同轮对照 %.2f ns（DUT/对照 %.2f×，仅披露、不作参考值）；"
                      "calibration: iters=%zu, 中位单批≈%.1f ms%s",
                      note, spread_text, reference_ns, kBaselineFile, control_sample.median_ns,
                      control_sample.median_ns > 0.0 ? sample.median_ns / control_sample.median_ns : 0.0,
                      static_cast<size_t>(sample.iters), batch_ms, ungraded_text);

        ID::Bench::RecordMetric({ .name         = name,
                                  .actual_ns    = sample.median_ns,
                                  .reference_ns = reference_ns,
                                  .basis        = "regression",
                                  .note         = buffer });
    }

    // ============================ 手写对照（纯 float 数组） ============================
    // 对照与 DUT 完全独立：只做 float 数组上的显式循环，不复用任何 backend 的运算符。

    ID_FORCEINLINE void control_add(const float* lhs, const float* rhs, float* out) noexcept
    {
        out[0] = lhs[0] + rhs[0];
        out[1] = lhs[1] + rhs[1];
        out[2] = lhs[2] + rhs[2];
        out[3] = lhs[3] + rhs[3];
    }

    ID_FORCEINLINE float control_dot(const float* lhs, const float* rhs) noexcept
    {
        float sum = 0.0f;
        for (ID::size_t i = 0; i < 4; ++i)
        {
            sum += lhs[i] * rhs[i];
        }
        return sum;
    }

    ID_FORCEINLINE void control_normalize(const float* source, float* out) noexcept
    {
        float squared = 0.0f;
        for (ID::size_t i = 0; i < 4; ++i)
        {
            squared += source[i] * source[i];
        }
        const float inverse_length = 1.0f / std::sqrt(squared);
        for (ID::size_t i = 0; i < 4; ++i)
        {
            out[i] = source[i] * inverse_length;
        }
    }

    ID_FORCEINLINE void control_matvec(const float* m, const float* v, float* out) noexcept
    {
        for (ID::size_t r = 0; r < 4; ++r)
        {
            float sum = 0.0f;
            for (ID::size_t c = 0; c < 4; ++c)
            {
                sum += m[c * 4 + r] * v[c];
            }
            out[r] = sum;
        }
    }

    ID_FORCEINLINE void control_matmul(const float* lhs, const float* rhs, float* out) noexcept
    {
        for (ID::size_t c = 0; c < 4; ++c)
        {
            for (ID::size_t r = 0; r < 4; ++r)
            {
                float sum = 0.0f;
                for (ID::size_t k = 0; k < 4; ++k)
                {
                    sum += lhs[k * 4 + r] * rhs[c * 4 + k];
                }
                out[c * 4 + r] = sum;
            }
        }
    }

    // ============================ DUT 探测体与对照 ============================

    // 加法的加数必须**逐迭代轮转**：常量加数在 -ffast-math 下会被编译器折叠成 `acc + N×b`
    // （实测 Release 生成 `vfmadd231ps` 并直接消掉整个循环，读数退化为空循环），
    // 因此这里从 64 个元素的数组按 `i & 63` 轮转取值，每个迭代都是一次真实加法 + 一次 L1 load。
    struct AddLatencyProbe
    {
        const vec4* m_addends = nullptr;      // 64 个轮转加数
        vec4        m_seed;
        float       sink      = 0.0f;

        ID_NOINLINE void operator()(ID::size_t iters) noexcept
        {
            vec4 accumulator = m_seed;
            for (ID::size_t i = 0; i < iters; ++i)
            {
                accumulator = accumulator + m_addends[i & (kElementCount - 1)];   // 单一 loop-carried 链
            }
            sink = accumulator[0] + accumulator[1] + accumulator[2] + accumulator[3];
        }
    };

    struct AddLatencyControl
    {
        const float* m_addends = nullptr;     // 64 × 4 个 float
        float        m_seed[4] = {};
        float        sink      = 0.0f;

        ID_NOINLINE void operator()(ID::size_t iters) noexcept
        {
            float accumulator[4] = { m_seed[0], m_seed[1], m_seed[2], m_seed[3] };
            for (ID::size_t i = 0; i < iters; ++i)
            {
                control_add(accumulator, m_addends + (i & (kElementCount - 1)) * 4, accumulator);
            }
            sink = accumulator[0] + accumulator[1] + accumulator[2] + accumulator[3];
        }
    };

    struct AddThroughputProbe
    {
        const vec4* m_addends = nullptr;
        vec4        m_seed;
        float       sink      = 0.0f;

        ID_NOINLINE void operator()(ID::size_t iters) noexcept
        {
            constexpr ID::size_t stride = kElementCount / 4;
            vec4 accumulator0 = m_seed;
            vec4 accumulator1 = m_seed;
            vec4 accumulator2 = m_seed;
            vec4 accumulator3 = m_seed;
            for (ID::size_t i = 0; i < iters; ++i)
            {
                accumulator0 = accumulator0 + m_addends[(i + 0 * stride) & (kElementCount - 1)];
                accumulator1 = accumulator1 + m_addends[(i + 1 * stride) & (kElementCount - 1)];
                accumulator2 = accumulator2 + m_addends[(i + 2 * stride) & (kElementCount - 1)];
                accumulator3 = accumulator3 + m_addends[(i + 3 * stride) & (kElementCount - 1)];
            }
            const vec4 total = accumulator0 + accumulator1 + accumulator2 + accumulator3;
            sink = total[0] + total[1] + total[2] + total[3];
        }
    };

    struct AddThroughputControl
    {
        const float* m_addends = nullptr;
        float        m_seed[4] = {};
        float        sink      = 0.0f;

        ID_NOINLINE void operator()(ID::size_t iters) noexcept
        {
            constexpr ID::size_t stride = kElementCount / 4;
            float accumulator0[4] = { m_seed[0], m_seed[1], m_seed[2], m_seed[3] };
            float accumulator1[4] = { m_seed[0], m_seed[1], m_seed[2], m_seed[3] };
            float accumulator2[4] = { m_seed[0], m_seed[1], m_seed[2], m_seed[3] };
            float accumulator3[4] = { m_seed[0], m_seed[1], m_seed[2], m_seed[3] };
            for (ID::size_t i = 0; i < iters; ++i)
            {
                control_add(accumulator0, m_addends + ((i + 0 * stride) & (kElementCount - 1)) * 4, accumulator0);
                control_add(accumulator1, m_addends + ((i + 1 * stride) & (kElementCount - 1)) * 4, accumulator1);
                control_add(accumulator2, m_addends + ((i + 2 * stride) & (kElementCount - 1)) * 4, accumulator2);
                control_add(accumulator3, m_addends + ((i + 3 * stride) & (kElementCount - 1)) * 4, accumulator3);
            }
            sink = accumulator0[0] + accumulator1[0] + accumulator2[0] + accumulator3[0];
        }
    };

    struct DotProbe
    {
        const vec4* m_values = nullptr;
        float       sink     = 0.0f;

        ID_NOINLINE void operator()(ID::size_t iters) noexcept
        {
            float sum = 0.0f;
            for (ID::size_t i = 0; i < iters; ++i)
            {
                sum += ID::Math::Scalar::dot(m_values[i & (kElementCount - 1)], m_values[(i >> 6) & 7]);
            }
            sink = sum;
        }
    };

    struct DotControl
    {
        const float* m_values = nullptr;      // 64 × 4 个 float
        float        sink     = 0.0f;

        ID_NOINLINE void operator()(ID::size_t iters) noexcept
        {
            float sum = 0.0f;
            for (ID::size_t i = 0; i < iters; ++i)
            {
                const float* lhs = m_values + (i & (kElementCount - 1)) * 4;
                const float* rhs = m_values + ((i >> 6) & 7) * 4;
                sum += control_dot(lhs, rhs);
            }
            sink = sum;
        }
    };

    struct NormalizeProbe
    {
        const vec4* m_values = nullptr;
        vec4        m_accumulator;
        float       sink     = 0.0f;

        ID_NOINLINE void operator()(ID::size_t iters) noexcept
        {
            vec4 accumulator = m_accumulator;
            for (ID::size_t i = 0; i < iters; ++i)
            {
                // 附带一次向量累加用于消费结果（避免整条链被删除）；该附加开销计入读数，两侧相同
                accumulator = accumulator + ID::Math::Scalar::normalize_unchecked(m_values[i & (kElementCount - 1)]);
            }
            sink = accumulator[0] + accumulator[1] + accumulator[2] + accumulator[3];
        }
    };

    struct NormalizeControl
    {
        const float* m_values       = nullptr;
        float        m_accumulator[4] = {};
        float        sink           = 0.0f;

        ID_NOINLINE void operator()(ID::size_t iters) noexcept
        {
            float accumulator[4] = { m_accumulator[0], m_accumulator[1], m_accumulator[2], m_accumulator[3] };
            for (ID::size_t i = 0; i < iters; ++i)
            {
                float normalized[4] = {};
                control_normalize(m_values + (i & (kElementCount - 1)) * 4, normalized);
                control_add(accumulator, normalized, accumulator);
            }
            sink = accumulator[0] + accumulator[1] + accumulator[2] + accumulator[3];
        }
    };

    struct AosProbe
    {
        const vec4* m_values = nullptr;       // AoS：64 个连续 vec4
        const vec4* m_rhs    = nullptr;
        float       sink     = 0.0f;

        ID_NOINLINE void operator()(ID::size_t iters) noexcept
        {
            float sum = 0.0f;
            for (ID::size_t i = 0; i < iters; ++i)
            {
                sum += ID::Math::Scalar::dot(m_values[i & (kElementCount - 1)], m_rhs[(i >> 6) & 7]);
            }
            sink = sum;
        }
    };

    struct AosControl
    {
        const float* m_values = nullptr;      // AoS：64 × 4 个 float
        const float* m_rhs    = nullptr;
        float        sink     = 0.0f;

        ID_NOINLINE void operator()(ID::size_t iters) noexcept
        {
            float sum = 0.0f;
            for (ID::size_t i = 0; i < iters; ++i)
            {
                sum += control_dot(m_values + (i & (kElementCount - 1)) * 4, m_rhs + ((i >> 6) & 7) * 4);
            }
            sink = sum;
        }
    };

    struct SoaProbe
    {
        const float* m_x  = nullptr;          // SoA：4 个分量数组，各 64 个 float
        const float* m_y  = nullptr;
        const float* m_z  = nullptr;
        const float* m_w  = nullptr;
        vec4         m_rhs;
        float        sink = 0.0f;

        ID_NOINLINE void operator()(ID::size_t iters) noexcept
        {
            float sum = 0.0f;
            for (ID::size_t i = 0; i < iters; ++i)
            {
                const ID::size_t index = i & (kElementCount - 1);
                const vec4       value(m_x[index], m_y[index], m_z[index], m_w[index]);
                sum += ID::Math::Scalar::dot(value, m_rhs);
            }
            sink = sum;
        }
    };

    struct SoaControl
    {
        const float* m_x   = nullptr;
        const float* m_y   = nullptr;
        const float* m_z   = nullptr;
        const float* m_w   = nullptr;
        const float* m_rhs = nullptr;
        float        sink  = 0.0f;

        ID_NOINLINE void operator()(ID::size_t iters) noexcept
        {
            float sum = 0.0f;
            for (ID::size_t i = 0; i < iters; ++i)
            {
                const ID::size_t index       = i & (kElementCount - 1);
                const float      gathered[4] = { m_x[index], m_y[index], m_z[index], m_w[index] };
                sum += control_dot(gathered, m_rhs);
            }
            sink = sum;
        }
    };

    struct MatVecProbe
    {
        const matrix<4, 4>* m_target = nullptr;
        const vec4*         m_values = nullptr;
        vec4                m_accumulator;
        float               sink     = 0.0f;

        ID_NOINLINE void operator()(ID::size_t iters) noexcept
        {
            vec4 accumulator = m_accumulator;
            for (ID::size_t i = 0; i < iters; ++i)
            {
                accumulator = accumulator + (*m_target * m_values[i & (kElementCount - 1)]);
            }
            sink = accumulator[0] + accumulator[1] + accumulator[2] + accumulator[3];
        }
    };

    struct MatVecControl
    {
        const float* m_target      = nullptr;      // 4×4 列主序
        const float* m_values      = nullptr;      // 64 × 4
        float        m_accumulator[4] = {};
        float        sink          = 0.0f;

        ID_NOINLINE void operator()(ID::size_t iters) noexcept
        {
            float accumulator[4] = { m_accumulator[0], m_accumulator[1], m_accumulator[2], m_accumulator[3] };
            for (ID::size_t i = 0; i < iters; ++i)
            {
                float product[4] = {};
                control_matvec(m_target, m_values + (i & (kElementCount - 1)) * 4, product);
                control_add(accumulator, product, accumulator);
            }
            sink = accumulator[0] + accumulator[1] + accumulator[2] + accumulator[3];
        }
    };

    struct MatMulProbe
    {
        const matrix<4, 4>* m_lhs = nullptr;       // 8 个左矩阵
        const matrix<4, 4>* m_rhs = nullptr;       // 8 个右矩阵
        float               sink  = 0.0f;

        ID_NOINLINE void operator()(ID::size_t iters) noexcept
        {
            float sum = 0.0f;
            for (ID::size_t i = 0; i < iters; ++i)
            {
                const matrix<4, 4> product = m_lhs[i & (kMatrixCount - 1)] * m_rhs[(i >> 3) & (kMatrixCount - 1)];
                // 消费全部 16 个元素：只读一个元素会让其余列被死存储消除（DUT 与对照同样处理）
                float partial = 0.0f;
                for (ID::size_t r = 0; r < 4; ++r)
                {
                    for (ID::size_t c = 0; c < 4; ++c)
                    {
                        partial += product.get_element(r, c);
                    }
                }
                sum += partial;
            }
            sink = sum;
        }
    };

    struct MatMulControl
    {
        const float* m_lhs = nullptr;               // 8 × 16 个 float，列主序
        const float* m_rhs = nullptr;
        float        sink  = 0.0f;

        ID_NOINLINE void operator()(ID::size_t iters) noexcept
        {
            float sum = 0.0f;
            for (ID::size_t i = 0; i < iters; ++i)
            {
                float product[16] = {};
                control_matmul(m_lhs + (i & (kMatrixCount - 1)) * 16,
                               m_rhs + ((i >> 3) & (kMatrixCount - 1)) * 16, product);
                float partial = 0.0f;
                for (ID::size_t index = 0; index < 16; ++index)
                {
                    partial += product[index];
                }
                sum += partial;
            }
            sink = sum;
        }
    };

    struct AccessConstProbe
    {
        const matrix<4, 4>* m_targets = nullptr;   // 8 个矩阵
        float               sink      = 0.0f;

        ID_NOINLINE void operator()(ID::size_t iters) noexcept
        {
            float sum = 0.0f;
            for (ID::size_t i = 0; i < iters; ++i)
            {
                // 常量索引；经 volatile 指针强制每个迭代真实执行一次 L1 load
                const volatile float* element = &m_targets[i & (kMatrixCount - 1)].get_element(1, 2);
                sum += *element;
            }
            sink = sum;
        }
    };

    struct AccessConstControl
    {
        const float* m_targets = nullptr;          // 8 × 16 个 float（列主序）
        float        sink      = 0.0f;

        ID_NOINLINE void operator()(ID::size_t iters) noexcept
        {
            float sum = 0.0f;
            for (ID::size_t i = 0; i < iters; ++i)
            {
                const volatile float* element = m_targets + (i & (kMatrixCount - 1)) * 16 + (2 * 4 + 1);
                sum += *element;
            }
            sink = sum;
        }
    };

    struct AccessDynamicProbe
    {
        const matrix<4, 4>* m_targets = nullptr;
        float               sink      = 0.0f;

        ID_NOINLINE void operator()(ID::size_t iters) noexcept
        {
            float sum = 0.0f;
            for (ID::size_t i = 0; i < iters; ++i)
            {
                const ID::size_t      row     = i & 3;
                const ID::size_t      column  = (i >> 2) & 3;
                const volatile float* element = &m_targets[(i >> 4) & (kMatrixCount - 1)].get_element(row, column);
                sum += *element;
            }
            sink = sum;
        }
    };

    struct AccessDynamicControl
    {
        const float* m_targets = nullptr;
        float        sink      = 0.0f;

        ID_NOINLINE void operator()(ID::size_t iters) noexcept
        {
            float sum = 0.0f;
            for (ID::size_t i = 0; i < iters; ++i)
            {
                const ID::size_t      row     = i & 3;
                const ID::size_t      column  = (i >> 2) & 3;
                const volatile float* element =
                    m_targets + ((i >> 4) & (kMatrixCount - 1)) * 16 + column * 4 + row;
                sum += *element;
            }
            sink = sum;
        }
    };

    struct ProxyConstProbe
    {
        const matrix<4, 4>* m_targets = nullptr;
        float               sink      = 0.0f;

        ID_NOINLINE void operator()(ID::size_t iters) noexcept
        {
            float sum = 0.0f;
            for (ID::size_t i = 0; i < iters; ++i)
            {
                const volatile float* element = &m_targets[i & (kMatrixCount - 1)][1][2];
                sum += *element;
            }
            sink = sum;
        }
    };

    struct ProxyDynamicProbe
    {
        const matrix<4, 4>* m_targets = nullptr;
        float               sink      = 0.0f;

        ID_NOINLINE void operator()(ID::size_t iters) noexcept
        {
            float sum = 0.0f;
            for (ID::size_t i = 0; i < iters; ++i)
            {
                const ID::size_t      row     = i & 3;
                const ID::size_t      column  = (i >> 2) & 3;
                const volatile float* element = &m_targets[(i >> 4) & (kMatrixCount - 1)][row][column];
                sum += *element;
            }
            sink = sum;
        }
    };

    // ============================ 数据构造 ============================

    struct PerformanceData
    {
        vec4         vectors[kElementCount]                 = {};
        float        vector_floats[kElementCount * 4]       = {};
        vec4         rhs[kMatrixCount]                      = {};
        float        rhs_floats[kMatrixCount * 4]           = {};
        matrix<4, 4> matrices[kMatrixCount]                 = {};
        float        matrix_floats[kMatrixCount * 16]       = {};
        matrix<4, 4> right_matrices[kMatrixCount]           = {};
        float        right_matrix_floats[kMatrixCount * 16] = {};
        float        soa_x[kElementCount]                   = {};
        float        soa_y[kElementCount]                   = {};
        float        soa_z[kElementCount]                   = {};
        float        soa_w[kElementCount]                   = {};
    };

    void build_data(PerformanceData& data) noexcept
    {
        FixedRandom random(kSeedVectorA);
        for (ID::size_t i = 0; i < kElementCount; ++i)
        {
            float components[4] = {};
            for (ID::size_t c = 0; c < 4; ++c)
            {
                components[c] = random.next_signed();
            }
            data.vectors[i] = vec4(components[0], components[1], components[2], components[3]);
            for (ID::size_t c = 0; c < 4; ++c)
            {
                data.vector_floats[i * 4 + c] = components[c];
            }
        }

        FixedRandom rhs_random(kSeedVectorB);
        for (ID::size_t i = 0; i < kMatrixCount; ++i)
        {
            float components[4] = {};
            for (ID::size_t c = 0; c < 4; ++c)
            {
                components[c] = rhs_random.next_signed();
            }
            data.rhs[i] = vec4(components[0], components[1], components[2], components[3]);
            for (ID::size_t c = 0; c < 4; ++c)
            {
                data.rhs_floats[i * 4 + c] = components[c];
            }
        }

        FixedRandom matrix_random(kSeedMatrixA);
        for (ID::size_t i = 0; i < kMatrixCount; ++i)
        {
            matrix<4, 4> value;
            for (ID::size_t c = 0; c < 4; ++c)
            {
                for (ID::size_t r = 0; r < 4; ++r)
                {
                    const float element     = matrix_random.next_signed();
                    value.get_element(r, c) = element;
                    data.matrix_floats[i * 16 + c * 4 + r] = element;
                }
            }
            data.matrices[i] = value;
        }

        FixedRandom right_random(kSeedMatrixB);
        for (ID::size_t i = 0; i < kMatrixCount; ++i)
        {
            matrix<4, 4> value;
            for (ID::size_t c = 0; c < 4; ++c)
            {
                for (ID::size_t r = 0; r < 4; ++r)
                {
                    const float element     = right_random.next_signed();
                    value.get_element(r, c) = element;
                    data.right_matrix_floats[i * 16 + c * 4 + r] = element;
                }
            }
            data.right_matrices[i] = value;
        }

        FixedRandom soa_random(kSeedSoA);
        for (ID::size_t i = 0; i < kElementCount; ++i)
        {
            data.soa_x[i] = soa_random.next_signed();
            data.soa_y[i] = soa_random.next_signed();
            data.soa_z[i] = soa_random.next_signed();
            data.soa_w[i] = soa_random.next_signed();
        }
    }
} // namespace

namespace ID::Bench
{
    void IDMathScalarPerformanceTest() noexcept
    {
        // 全部数据都在栈上（约 20 KB）：不分配、不与其它域共享状态
        PerformanceData data;
        build_data(data);

        // 所有 note 的统一边界披露：basis=regression 的含义、比较边界与未闭合项（参考值来源见文件内基线表）
        // 行预算：benchmark_support 的 emit() 行缓冲是 1024 字节（kLineCapacity），
        // 本串 + 指标描述 + record() 的固定后缀 + ungraded 后缀共同组成一行，必须留出余量；
        // 超出后 emit() 会截断并吞掉行尾换行，把下一条指标粘到同一行（实测）。
        const char* const kBoundary =
            "basis=regression：参考值取本机同配置（同编译器/构建类型/flags/数据）历史基线 txt，"
            "允许回退 0%；ratio≈1 只表示相对基线未退化（未闭合项：绝对质量仍缺独立下界，"
            "regression≠快），不得跨机器/跨配置/跨 backend 借用";

        char note[1100] = {};

        // ---------- 1. 向量加法：单链延迟 ----------
        {
            AddLatencyProbe dut{ data.vectors, data.vectors[0], 0.0f };
            AddLatencyControl control{
                data.vector_floats,
                { data.vector_floats[0], data.vector_floats[1], data.vector_floats[2], data.vector_floats[3] },
                0.0f };
            Sample dut_sample{};
            Sample control_sample{};
            measure_pair(dut, control, 1, dut_sample, control_sample);

            std::snprintf(note, sizeof(note),
                          "%s；DUT = 单链：acc = acc + b（vec4 加法，唯一 loop-carried 依赖）；对照 = 同一链的"
                          "手写逐分量 float 实现（control_add）；加数按 i&63 从 64 元素数组轮转（常量加数会被 "
                          "-ffast-math 折叠成 acc+N×b 并消掉整个循环），每 op 含一次 16B L1 load；"
                          "Release 下对照与 DUT 生成逐字相同的链，故对照不能当参考值",
                          kBoundary);
            record("vec.add.latency.ns", dut_sample, control_sample, note);
        }

        // ---------- 2. 向量加法：独立吞吐（4 条互不相干的链） ----------
        {
            AddThroughputProbe dut{ data.vectors, data.vectors[0], 0.0f };
            AddThroughputControl control{
                data.vector_floats,
                { data.vector_floats[0], data.vector_floats[1], data.vector_floats[2], data.vector_floats[3] },
                0.0f };
            Sample dut_sample{};
            Sample control_sample{};
            measure_pair(dut, control, 4, dut_sample, control_sample);

            std::snprintf(note, sizeof(note),
                          "%s；DUT = 4 条独立链（每迭代 4 个 vec4 加法，折算 4 op/迭代）；对照 = 同一结构的"
                          "手写逐分量 float 实现（两侧机器码不一定相同，见机器码证据）；"
                          "4 路加数按不同偏移轮转，每 op 含一次 16B L1 load",
                          kBoundary);
            record("vec.add.throughput.ns", dut_sample, control_sample, note);
        }

        // ---------- 3. 点积 ----------
        {
            DotProbe   dut{ data.vectors, 0.0f };
            DotControl control{ data.vector_floats, 0.0f };
            Sample     dut_sample{};
            Sample     control_sample{};
            measure_pair(dut, control, 1, dut_sample, control_sample);

            std::snprintf(note, sizeof(note),
                          "%s；DUT = dot(v[i&63], v[(i>>6)&7])（4 分量内积）；对照 = 同一对 float 上的手写累加；"
                          "两个操作数按不同周期轮转（周期 448），避免循环不变量被提升",
                          kBoundary);
            record("vec.dot.ns", dut_sample, control_sample, note);
        }

        // ---------- 4. 归一化 ----------
        {
            NormalizeProbe dut{ data.vectors, data.vectors[0], 0.0f };
            NormalizeControl control{
                data.vector_floats,
                { data.vector_floats[4], data.vector_floats[5], data.vector_floats[6], data.vector_floats[7] },
                0.0f };
            Sample dut_sample{};
            Sample control_sample{};
            measure_pair(dut, control, 1, dut_sample, control_sample);

            std::snprintf(note, sizeof(note),
                          "%s；DUT = normalize_unchecked(v[i&63]) 并累加消费（sqrt + 一次倒数 + 逐分量乘）；"
                          "对照 = 同一算法的纯 float 手写实现（control_normalize + control_add）；"
                          "每 op 附带一次向量累加用于消费结果，该附加开销计入读数（两侧相同）",
                          kBoundary);
            record("vec.normalize.ns", dut_sample, control_sample, note);
        }

        // ---------- 5. AoS ----------
        {
            AosProbe   dut{ data.vectors, data.rhs, 0.0f };
            AosControl control{ data.vector_floats, data.rhs_floats, 0.0f };
            Sample     dut_sample{};
            Sample     control_sample{};
            measure_pair(dut, control, 1, dut_sample, control_sample);

            std::snprintf(note, sizeof(note),
                          "%s；AoS 布局：64 个 vec4 连续排列；DUT = dot(aos[i&63], rhs[(i>>6)&7])；对照 = 同一块"
                          "浮点缓冲上的手写累加（指针偏移 4×index）；两条 AoS/SoA 指标的差异只来自布局",
                          kBoundary);
            record("vec.add.aos.ns", dut_sample, control_sample, note);
        }

        // ---------- 6. SoA ----------
        {
            SoaProbe   dut{ data.soa_x, data.soa_y, data.soa_z, data.soa_w, data.rhs[0], 0.0f };
            SoaControl control{ data.soa_x, data.soa_y, data.soa_z, data.soa_w, data.rhs_floats, 0.0f };
            Sample     dut_sample{};
            Sample     control_sample{};
            measure_pair(dut, control, 1, dut_sample, control_sample);

            std::snprintf(note, sizeof(note),
                          "%s；SoA 布局：4 个分量数组各 64 个 float；DUT = 先用 (x,y,z,w)[i] 构造 vec4 再 dot；"
                          "对照 = 同样的 gather + 手写累加（不复用 DUT 的构造路径）；"
                          "两侧都付一次 gather/构造代价，差异只来自布局与向量化",
                          kBoundary);
            record("vec.add.soa.ns", dut_sample, control_sample, note);
        }

        // ---------- 7. 矩阵乘向量 ----------
        {
            MatVecProbe dut{ &data.matrices[0], data.vectors, data.vectors[0], 0.0f };
            MatVecControl control{
                data.matrix_floats,
                data.vector_floats,
                { data.vector_floats[4], data.vector_floats[5], data.vector_floats[6], data.vector_floats[7] },
                0.0f };
            Sample dut_sample{};
            Sample control_sample{};
            measure_pair(dut, control, 1, dut_sample, control_sample);

            std::snprintf(note, sizeof(note),
                          "%s；matrix<4,4> * vec4（列向量约定）并累加消费；对照 = 同一 4×4 列主序 float 上的"
                          "三重循环 matvec + 加法；每 op 附带一次向量累加（两侧相同）",
                          kBoundary);
            record("mat.matvec.ns", dut_sample, control_sample, note);
        }

        // ---------- 8. 矩阵乘 ----------
        {
            MatMulProbe   dut{ data.matrices, data.right_matrices, 0.0f };
            MatMulControl control{ data.matrix_floats, data.right_matrix_floats, 0.0f };
            Sample        dut_sample{};
            Sample        control_sample{};
            measure_pair(dut, control, 1, dut_sample, control_sample);

            std::snprintf(note, sizeof(note),
                          "%s；matrix<4,4> * matrix<4,4>，结果 16 个元素全部累加消费（否则死存储消除会删掉未读"
                          "列）；对照组合同样累加 16 个元素；左右矩阵按 8×8 周期轮转",
                          kBoundary);
            record("mat.matmul.ns", dut_sample, control_sample, note);
        }

        // ---------- 9/10. 访问器：get_element 的常量索引与动态索引 ----------
        {
            AccessConstProbe   dut{ data.matrices, 0.0f };
            AccessConstControl control{ data.matrix_floats, 0.0f };
            Sample             dut_sample{};
            Sample             control_sample{};
            measure_pair(dut, control, 1, dut_sample, control_sample);

            std::snprintf(note, sizeof(note),
                          "%s；DUT = m[i&7].get_element(1, 2)（常量索引，偏移 2×4+1）；对照 = float 缓冲上同一"
                          "常量偏移；两侧都经 volatile 指针读取（强制每次迭代真实执行一次 L1 load，否则 load 会"
                          "被不变量提升）；Release 下两者生成同一条指令，对照不能当参考值",
                          kBoundary);
            record("mat.access.const_index.ns", dut_sample, control_sample, note);
        }
        {
            AccessDynamicProbe   dut{ data.matrices, 0.0f };
            AccessDynamicControl control{ data.matrix_floats, 0.0f };
            Sample               dut_sample{};
            Sample               control_sample{};
            measure_pair(dut, control, 1, dut_sample, control_sample);

            std::snprintf(note, sizeof(note),
                          "%s；DUT = m[(i>>4)&7].get_element(i&3, (i>>2)&3)（动态行列）；对照 = float 缓冲上"
                          "column*4+row 的等价地址计算；两侧都经 volatile 读取",
                          kBoundary);
            record("mat.access.dynamic_index.ns", dut_sample, control_sample, note);
        }

        // ---------- 11/12. 行代理 m[row][col]（D0-04：不承诺零开销，只测量实际代价） ----------
        {
            ProxyConstProbe dut{ data.matrices, 0.0f };
            AccessConstControl control{ data.matrix_floats, 0.0f };
            Sample          dut_sample{};
            Sample          control_sample{};
            measure_pair(dut, control, 1, dut_sample, control_sample);

            std::snprintf(note, sizeof(note),
                          "%s；DUT = m[i&7][1][2]（行代理的常量索引）；对照 = 同一常量偏移的 float 读取"
                          "（Release 下与 DUT 同指令）；row_proxy 按 D0-04 **不承诺**零开销，本指标只报"
                          "实际代价，不构成升级为零开销承诺的证据",
                          kBoundary);
            record("mat.access.proxy_const_index.ns", dut_sample, control_sample, note);
        }
        {
            ProxyDynamicProbe dut{ data.matrices, 0.0f };
            AccessDynamicControl control{ data.matrix_floats, 0.0f };
            Sample            dut_sample{};
            Sample            control_sample{};
            measure_pair(dut, control, 1, dut_sample, control_sample);

            std::snprintf(note, sizeof(note),
                          "%s；DUT = m[(i>>4)&7][i&3][(i>>2)&3]（行代理的动态行列）；对照 = 同一地址计算的 float"
                          "读取；与上一条一起给出代理相对 get_element 的实际差距（不宣称零开销）",
                          kBoundary);
            record("mat.access.proxy_dynamic_index.ns", dut_sample, control_sample, note);
        }
    }
} // namespace ID::Bench

#else

namespace ID::Bench
{
    void IDMathScalarPerformanceTest() noexcept
    {
        Skip("math.scalar.performance",
             "该构建组合未选中该 backend：ID_USE_PLUGIN_IDMATHSCALAR 未定义（ID_ASSEMBLY_PROFILE 未选中 ID.Math.Scalar），本域不出任何指标行");
    }
} // namespace ID::Bench

#endif
