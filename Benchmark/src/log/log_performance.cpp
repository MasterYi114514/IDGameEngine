#include "benchmark.hpp"

#include <cstdio>

// 与 log_security.cpp 同理：IDLog 公开头与产品入口头只在选中 ID.Log 的构建里存在，
// 本项目头因此必须放在条件编译内，保证默认/空 profile 构建仍能编译出 Skip 版本。
#if defined(ID_USE_PLUGIN_IDLOG)
    #include "IDGameEngine.hpp"
    #include "IDLogLogger.hpp"
#endif

/**
 *  ID.Log 性能基准（Baseline1：rev2 服务表 + 静态 C++ 运行时）
 *  1. 只测当前产品真实可达的 per-op 路径，最终恰好 6 条指标：关闭级别的 method / macro 两种调用形态、
 *     打开级别的 async 短消息与长消息、队列排空时的 flush、reconfigure 出来的同步形态；
 *  2. reconfigure / 轮转重建属于**冷路径**（每次调用新建 sink/logger/thread_pool，销毁时 join 旧 worker）：
 *     1 次调用 = 1 个 op，5 轮极差观测 40–80%，与 per-op 热路径不可比，因此**不写官方 perf 行、不参与评级**，
 *     只在 Performance 段用 Skip 说明口径；功能正确性由 ID.Log Security 的 log.reconfigure.* 覆盖；
 *  3. 校准不再使用固定最小迭代数：从 iters=1 起步**倍增**，直到单批 ≥ kTargetRoundNs（100 ms）或撞 kMaxIters；
 *     于是慢操作（同步 sink 数十~数百 µs/op）只用几打到几万次迭代，快操作（≈0.3 ns/op）仍能凑满 100 ms。
 *     旧实现固定 kMinIters=65536：0.9 ms/op 的同步指标每批 ≈59 s、整套 ≈8 min；
 *  4. 预热 = 校准倍增段（逐级把缓存、分支预测、线程池与文件页摸热）+ 一次满尺寸非计时批；
 *     样本固定 5 轮取中位数；KeepAlive 在计时区间之外，计时循环内不做 Check、不取任何诊断字符串；
 *     单批目标默认 100 ms；flush 与 sync 也使用同一目标（加长到 500 ms 的对照实验未消除 stderr=file 抖动，统一回 100 ms）
 *     （100 ms 时同步指标只有约 100 次写调用，单次多毫秒级盘写就能把整轮拉偏，实测 5 轮极差 29–181%）；
 *  5. 每条指标都有**分配置** regression 参考值（MSVC Debug / MSVC Release / MinGW Debug / MinGW Release），
 *     由编译期分支唯一选择，绝不把某配置的参考值套到另一配置；未知配置直接编译失败。
 *     参考值来源 = Baseline1 phase1a（100 ms 单批）：同配置、同条件连续 3 次运行，取 3 次"5 轮中位数"的中位数（N=3）。
 *     例外：MSVC Debug / MSVC Release 的 log.enabled.sync.ns 与 MinGW Debug 的 log.flush.ns 取 Baseline1 phase2a
 *     （500 ms 单批）同配置三次运行的中位数；这三条 5 轮极差可能 >20%（报告标 unstable），但仍作回归门槛。
 *     `reference_ns` 仍为 0.0 表示该配置尚未标定（测量阶段），报告按 ungraded 输出并写明原因；
 *  6. 参考值只作**同配置回归门槛**，不是绝对质量结论，也不跨配置、跨机器或跨 sink 目标比较；
 *  7. DUT 的 console sink 写 stderr，不进 report.txt，也不参与 console/report 逐字比对；
 *  8. **测量口径限制（自动化运行）**：所有自动化运行都把进程 stderr 重定向到文件，DUT 的
 *     `stderr_color_sink_mt` 因此实际写的是一个磁盘文件句柄（CRT 的 stderr 无缓冲，每行一次写系统调用；
 *     `flush()` 路径还会 `fflush` 该 FILE）。所以 `log.enabled.*` / `log.flush.ns` / `log.enabled.sync.ns`
 *     的绝对 ns 值只在 "stderr→文件" 这一条件下成立，**依赖 sink 目标**，不能与真控制台运行、
 *     也不能与 Baseline0（rev1 + 动态 C++ 运行时）跨轮比较；每条 note 都保留
 *     `stderr=file for automated run` 披露，sync 指标在条件里再显式标一次。
 */

#if defined(ID_USE_PLUGIN_IDLOG)

namespace
{
    // 单批目标时长：低于 100 ms 就等于把调度噪声当读数，因此校准只允许向上满足、不允许下调。
    // flush / sync 也使用同一目标：加长到 500 ms 的对照实验没有消除 stderr=file 下的写系统调用抖动
    // （本机并发编译期间 5 轮极差仍达 29–181%），反而让协议与参考值来源不一致，因此统一回 100 ms
    constexpr double     kTargetRoundNs       = 100.0e6;
    // 校准起点：从 1 次迭代起步倍增，慢操作自然停在很小的迭代数上（不再有固定最小迭代数）
    constexpr ID::size_t kCalibrateStartIters = 1;
    // 迭代上限：0.3 ns/op 凑满 100 ms 约需 3.4e8 次；1<<30 留出约 320 ms 余量，
    // 同时保证倍增时不会越过 2^31（size_t 安全范围）
    constexpr ID::size_t kMaxIters            = 1u << 30;
    constexpr int        kRounds              = 5;

    // 分配置参考值容器：字段与 6 条指标一一对应；0.0 表示该配置尚未标定（ungraded 测量阶段）
    struct References
    {
        double disabled_method_ns = 0.0;
        double disabled_macro_ns  = 0.0;
        double flush_ns           = 0.0;
        double enabled_short_ns   = 0.0;
        double enabled_long_ns    = 0.0;
        double enabled_sync_ns    = 0.0;
    };

    // ---- 分配置 regression 参考值（编译期唯一选择点） ----
    // 依据：Baseline1 phase1（100 ms 单批）同配置连续 3 次运行，每条指标取 3 次"5 轮中位数"的中位数（N=3）；
    // 条件：automated run（stderr→文件）、SHARED 构建、ID_ENABLE_AVX2=ON、未改 DUT。
    // basis 固定 "regression"：只作同配置回归门槛，不代表绝对质量，不允许跨配置借用。
    // 不稳定单元的口径（按 Baseline1 建 regression 门槛，不再留 ungraded）：
    // MSVC Debug / MSVC Release 的 enabled_sync_ns 与 MinGW Debug 的 flush_ns 这**三个值取 phase2a（100 ms 单批）
    // 三次运行"5 轮中位数"的中位数**（N=3）；它们 5 轮极差可能 >20%，报告会标 unstable，但仍作 regression 门槛。
    // kFlushUngradedReason / kSyncUngradedReason 机制保留：测量阶段或将来新增未标定单元仍用它写明 ungraded 原因。
    #if defined(_MSC_VER) && defined(_DEBUG)
        // 来源：Benchmark/build/msvc-log-rev2/baseline1-phase1-debug-run{1,2,3}.txt；
        // enabled_sync_ns 例外取 baseline1-phase2a-debug-run{1,2,3}.txt（phase2a，500 ms 单批）
        constexpr char       kReferenceProfile[] = "msvc-debug";
        constexpr References kReferences{
            .disabled_method_ns =    55.64,
            .disabled_macro_ns  =     3.60,
            .flush_ns           =   952.40,
            .enabled_short_ns   =  1923.53,
            .enabled_long_ns    =  3606.65,
            .enabled_sync_ns    = 849956.25     // phase2a 100 ms 批中位数：可能 unstable，仍作回归门槛
        };
        constexpr char kFlushUngradedReason[] = "";
        constexpr char kSyncUngradedReason[]  = "";
    #elif defined(_MSC_VER) && defined(NDEBUG)
        // 来源：Benchmark/build/msvc-log-rev2/baseline1-phase1-release-run{1,2,3}.txt；
        // enabled_sync_ns 例外取 baseline1-phase2a-release-run{1,2,3}.txt（phase2a，500 ms 单批）
        constexpr char       kReferenceProfile[] = "msvc-release";
        constexpr References kReferences{
            .disabled_method_ns =     0.36,
            .disabled_macro_ns  =     0.36,
            .flush_ns           =    95.29,
            .enabled_short_ns   =   145.93,
            .enabled_long_ns    =   412.18,
            .enabled_sync_ns    = 827158.59     // phase2a 100 ms 批中位数：可能 unstable，仍作回归门槛
        };
        constexpr char kFlushUngradedReason[] = "";
        constexpr char kSyncUngradedReason[]  = "";
    #elif !defined(_MSC_VER) && !defined(NDEBUG)
        // 来源：Benchmark/build/log-mingw-debug/baseline1-phase1-run{1,2,3}.txt；
        // flush_ns 例外取同目录 baseline1-phase2a-run{1,2,3}.txt（phase2a，500 ms 单批）
        constexpr char       kReferenceProfile[] = "mingw-debug";
        constexpr References kReferences{
            .disabled_method_ns =     3.18,
            .disabled_macro_ns  =     2.00,
            .flush_ns           =   998.92,     // phase2a 100 ms 批中位数：可能 unstable，仍作回归门槛
            .enabled_short_ns   =   538.47,
            .enabled_long_ns    =   648.42,
            .enabled_sync_ns    = 800231.25
        };
        constexpr char kFlushUngradedReason[] = "";
        constexpr char kSyncUngradedReason[]  = "";
    #elif !defined(_MSC_VER) && defined(NDEBUG)
        // 来源：Benchmark/build/log-rev2/baseline1-phase1-run{1,2,3}.txt
        constexpr char       kReferenceProfile[] = "mingw-release";
        constexpr References kReferences{
            .disabled_method_ns =     0.26,
            .disabled_macro_ns  =     0.35,
            .flush_ns           =   209.86,
            .enabled_short_ns   =   137.14,
            .enabled_long_ns    =   168.37,
            .enabled_sync_ns    = 811372.66
        };
        constexpr char kFlushUngradedReason[] = "";
        constexpr char kSyncUngradedReason[]  = "";
    #else
        #error "ID.Log regression 参考值未覆盖当前配置：不得借用其它配置的参考值，必须先补测该配置"
    #endif

    // 每条 note 的固定前缀：基线定义 + 参考值来源 + 测量条件 + 与 Baseline0 不可比。
    // 英文句子是披露口径的原文，改动会破坏跨报告可比性，不要翻译或省略
    constexpr char kBaselineNote[] =
        "Baseline1 same-config median(N=3), regression gate only; not absolute quality; "
        "stderr=file for automated run; Baseline0(rev1+dynamic C++ runtime) 不可比";

    // 长消息 payload：≈294 字节，加上 "long probe " 前缀后远超 spdlog memory_buf 的 250 字节内联容量，
    // 又明显小于 kStackFormatCapacity - 1，因此测的是"触发一次缓冲分配"而不是截断。
    constexpr char kLongPayload[] =
        "long payload segment 0123456789 abcdefghijklmnopqrstuvwxyz "
        "long payload segment 0123456789 abcdefghijklmnopqrstuvwxyz "
        "long payload segment 0123456789 abcdefghijklmnopqrstuvwxyz "
        "long payload segment 0123456789 abcdefghijklmnopqrstuvwxyz "
        "long payload segment 0123456789 abcdefghijklmnopqrstuvwxyz";

    struct Sample
    {
        double      median_ns = 0.0;     // 5 轮中位数，报告值
        double      min_ns    = 0.0;
        double      max_ns    = 0.0;
        double      spread    = 0.0;     // (max - min) / median，用于判定 unstable
        ID::size_t  iters     = 0;       // 校准得到的单批迭代数（报告里作为"单批够不够 100 ms"的证据）
    };

    /**
     *  @brief 跑一批 op 并单独计时
     *  @param op 被测操作；结果写进 op.sink
     *  @param iters 本批迭代数
     *  @return 整批经过时间（ns）
     *  @note KeepAlive 在计时区间之外调用：既让本批写入无法被删除，也不把一次 volatile 写算进指标
     */
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

    /**
     *  @brief 用倍增法标定单批迭代数：从 1 起步，直到单批 ≥ kTargetRoundNs 或撞 kMaxIters
     *  @param op 被测操作
     *  @return 单批迭代数
     *  @note 1. 不用固定最小迭代数：慢操作（同步 sink 0.9 ms/op）用 65536 会变成每批 59 s；
     *        2. 倍增段本身就是预热（缓存、分支预测、线程池、文件页逐级摸热），不计入样本；
     *        3. 早期批次的经过时间可能只有几个 QPC tick（甚至 0）：只要没到目标时长就继续倍增，
     *           不拿亚分辨率读数外推迭代数；
     *        4. 全程不做 Check：校准里出现分支/字符串比较会污染后面的测量
     */
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

    /**
     *  @brief 把 count 个样本排序后取中位数 / 极值 / 极差
     *  @param samples 样本数组（会被就地排序）
     *  @param count 样本个数，必须 >= 1
     *  @param iters 本次测量使用的单批迭代数，原样带进 Sample 供 note 披露
     *  @return 中位数、最小、最大、(max-min)/median 与迭代数
     *  @note 中位数抗单次调度抢占；均值会被一次抢占拉偏，因此不用均值
     */
    Sample summarize_samples(double* samples, int count, ID::size_t iters) noexcept
    {
        // 插入排序：样本数固定很小，不值得引入其它排序
        for (int i = 1; i < count; ++i)
        {
            const double key = samples[i];
            int j = i - 1;
            while (j >= 0 && samples[j] > key)
            {
                samples[j + 1] = samples[j];
                --j;
            }
            samples[j + 1] = key;
        }

        Sample sample{};
        sample.min_ns    = samples[0];
        sample.max_ns    = samples[count - 1];
        sample.median_ns = samples[count / 2];
        sample.spread    = sample.median_ns > 0.0 ? (sample.max_ns - sample.min_ns) / sample.median_ns : 0.0;
        sample.iters     = iters;
        return sample;
    }

    /**
     *  @brief 一条指标的完整测量流程：倍增校准 → 满尺寸非计时预热 → 5 轮计时取中位数
     *  @param op 被测操作
     *  @return 中位数、极值与校准后的迭代数
     */
    template <typename Op>
    Sample measure(Op& op) noexcept
    {
        const ID::size_t iters = calibrate(op);

        // 一次整批非计时预热：校准段最后一批虽然已 ≥100 ms，但它是"由冷到热"的过渡批，
        // 这里再跑一批满尺寸的，保证进入样本前缓存、分支与 sink 都已在目标工作量下热透
        static_cast<void>(time_batch(op, iters));

        double samples[kRounds] = {};
        for (int round = 0; round < kRounds; ++round)
        {
            samples[round] = time_batch(op, iters) / static_cast<double>(iters);
        }
        return summarize_samples(samples, kRounds, iters);
    }

    /**
     *  @brief 记录一条 ID.Log 指标：分配置 regression 参考值 + 校准证据 + unstable 标记
     *  @param name 指标名，跨轮次保持稳定
     *  @param sample 5 轮样本（含校准后的迭代数）
     *  @param reference_ns 当前配置的参考值；<= 0 表示该配置尚未标定（ungraded）
     *  @param note 测量条件（不含固定前缀，函数内统一加）
     *  @note unstable 标记放在校准信息之前，避免超长 note 被行容量截断时丢掉不稳定性披露
     */
    void record(const char* name, const Sample& sample, double reference_ns,
                const char* ungraded_reason, const char* note) noexcept
    {
        #if defined(ID_BENCH_LOG_REFERENCE_UNSET)
            // 测量阶段（CMake -DID_BENCH_LOG_REFERENCE_UNSET=ON）：参考值不参与评级，
            // 报告按 ungraded 输出，避免拿未标定/待复核的值冒充回归门槛
            reference_ns    = 0.0;
            ungraded_reason = "测量阶段（ID_BENCH_LOG_REFERENCE_UNSET=ON）：该配置 reference 未标定";
        #endif

        if (reference_ns > 0.0)
        {
            ungraded_reason = "";
        }
        else if (ungraded_reason[0] == '\0')
        {
            ungraded_reason = "reference=0（该配置尚未标定）";
        }

        char         spread_text[64] = {};
        if (sample.spread > 0.20)
        {
            std::snprintf(spread_text, sizeof(spread_text), "；unstable: 5 轮极差 %.0f%%", sample.spread * 100.0);
        }
        const char* const unstable = (sample.spread > 0.20) ? spread_text : "";
        const double      batch_ms = sample.median_ns * static_cast<double>(sample.iters) / 1.0e6;

        char ungraded_text[360] = {};
        if (reference_ns <= 0.0)
        {
            std::snprintf(ungraded_text, sizeof(ungraded_text), "；ungraded: %s", ungraded_reason);
        }

        char buffer[1600] = {};
        std::snprintf(buffer, sizeof(buffer), "%s%s；calibration: iters=%zu, 中位单批≈%.1f ms%s",
                      note, unstable, sample.iters, batch_ms, ungraded_text);

        ID::Bench::RecordMetric({ .name         = name,
                                  .actual_ns    = sample.median_ns,
                                  .reference_ns = reference_ns,
                                  .basis        = "regression",
                                  .note         = buffer });
    }

    // ---- 被测操作：级别关闭时的两种调用形态 ----
    // 两者都用变化的实参参与累加，保证编译器既不能折叠循环，也不能把被测调用提到循环外。

    struct DisabledMethodProbe
    {
        const ID::Log::Logger* logger = nullptr;
        ID::uintptr_t          sink   = 0;

        void operator()(ID::size_t iters) noexcept
        {
            ID::uintptr_t accumulator = 0;
            for (ID::size_t i = 0; i < iters; ++i)
            {
                logger->info("disabled method probe {}", static_cast<ID::uint32_t>(i));
                accumulator += i;
            }
            sink = accumulator;
        }
    };

    struct DisabledMacroProbe
    {
        const ID::Log::Logger* logger = nullptr;
        ID::uintptr_t          sink   = 0;

        void operator()(ID::size_t iters) noexcept
        {
            ID::uintptr_t accumulator = 0;
            for (ID::size_t i = 0; i < iters; ++i)
            {
                ID_LOG_INFO(logger, "disabled macro probe {}", static_cast<ID::uint32_t>(i));
                accumulator += i;
            }
            sink = accumulator;
        }
    };

    // ---- 被测操作：级别打开时的短消息与长消息 ----

    struct EnabledShortProbe
    {
        const ID::Log::Logger* logger = nullptr;
        ID::uintptr_t          sink   = 0;

        void operator()(ID::size_t iters) noexcept
        {
            ID::uintptr_t accumulator = 0;
            for (ID::size_t i = 0; i < iters; ++i)
            {
                // 格式化后 ≈33 字节：低于 spdlog 250 字节内联容量，不做堆分配
                logger->info("short probe id={} state=ready", static_cast<ID::uint32_t>(i));
                accumulator += i;
            }
            sink = accumulator;
        }
    };

    struct EnabledLongProbe
    {
        const ID::Log::Logger* logger = nullptr;
        ID::uintptr_t          sink   = 0;

        void operator()(ID::size_t iters) noexcept
        {
            ID::uintptr_t accumulator = 0;
            for (ID::size_t i = 0; i < iters; ++i)
            {
                // ≈305 字节：超过 250 字节内联容量，log_msg_buffer 必然扩容
                logger->info("long probe {}", static_cast<const char*>(kLongPayload));
                accumulator += i;
            }
            sink = accumulator;
        }
    };

    struct FlushProbe
    {
        const ID::Log::LogApiTable* table = nullptr;
        ID::uintptr_t               sink  = 0;

        void operator()(ID::size_t iters) noexcept
        {
            ID::uintptr_t accumulator = 0;
            for (ID::size_t i = 0; i < iters; ++i)
            {
                table->flush();
                accumulator += i;
            }
            sink = accumulator;
        }
    };
} // namespace

namespace ID::Bench
{
    void IDLogPerformanceTest() noexcept
    {
        const ID::PluginRuntimeDiagnostic started = ID::Engine::start();
        if (!started.is_ok())
        {
            Check(false, "perf.log.engine.start",
                  "性能指标以产品 Ready + 默认 LogConfig 为前提，start 必须成功");
            return;
        }

        const ID::Log::LogApiTable* const table =
            ID::PluginRuntime::instance().get_service<ID::Log::LogApiTable>(ID::ServiceSlot::Log);
        const ID::Log::Logger* const logger =
            (table != nullptr) ? ID::Log::acquire_logger(*table, "Bench.Perf") : nullptr;
        if (table == nullptr || logger == nullptr)
        {
            Check(false, "perf.log.table_and_logger",
                  "服务表与 Bench.Perf 句柄都必须可用，否则指标无意义");
            static_cast<void>(ID::Engine::shutdown());
            return;
        }

        // 每条 note 的统一前缀：基线定义 + 参考值来源 + 测量条件 + 与 Baseline0 不可比。
        // 容量按固定披露 + 配置名留足，snprintf 截断会静默吃掉口径披露
        char note_prefix[512] = {};
        std::snprintf(note_prefix, sizeof(note_prefix),
                      "%s；ref-profile=%s；reference=同配置 Baseline1 phase1 三次运行 5 轮中位数的中位数",
                      kBaselineNote, kReferenceProfile);

        // 每条指标的条件描述单独拼进 note（容量按最长一条 sync 留足）
        char note[1200] = {};

        // ---------- 关闭级别：函数调用形态与宏形态（默认 console + async 拓扑） ----------
        {
            ID::Log::LogConfig quiet{};
            quiet.min_level = static_cast<ID::uint32_t>(ID::Log::Level::Error);
            if (!table->configure(quiet))
            {
                Check(false, "perf.log.configure_quiet",
                      "把 min_level 调到 Error 必须成功，否则关闭级别指标不成立");
                static_cast<void>(ID::Engine::shutdown());
                return;
            }

            DisabledMethodProbe method_probe{ logger, 0 };
            std::snprintf(note, sizeof(note), "%s；min_level=Error：一次 relaxed load + 分支，"
                          "实参仍按函数调用语义求值（函数形态固有）；console+async 默认配置，消息不进队列",
                          note_prefix);
            record("log.disabled.method.ns", measure(method_probe), kReferences.disabled_method_ns, "", note);

            DisabledMacroProbe macro_probe{ logger, 0 };
            std::snprintf(note, sizeof(note), "%s；min_level=Error：ID_LOG_INFO 判空 + is_enabled "
                          "后跳过 write_at，实参不求值；与 method 用同一句柄、同一循环形态",
                          note_prefix);
            record("log.disabled.macro.ns", measure(macro_probe), kReferences.disabled_macro_ns, "", note);

            // flush 在队列已排空时测量（上面两条指标的消息全部被过滤），代价最可复现；
            // 积压清空路径与总排空耗时不在本指标内
            FlushProbe flush_probe{ table, 0 };
            std::snprintf(note, sizeof(note), "%s；队列已排空时 flush = 投递 flush 屏障"
                          "（队列锁 + push + 唤醒消费者）+ 同步 fatal logger flush sink 的 fflush(stderr)"
                          "（本次 stderr=file 时是真实 CRT 刷写）；不等待屏障完成，不代表落盘",
                          note_prefix);
            record("log.flush.ns", measure(flush_probe), kReferences.flush_ns, kFlushUngradedReason, note);
        }

        // ---------- 打开级别：短消息 / 长消息（async 入队） ----------
        {
            ID::Log::LogConfig loud{};
            loud.min_level = static_cast<ID::uint32_t>(ID::Log::Level::Info);
            if (!table->configure(loud))
            {
                Check(false, "perf.log.configure_loud",
                      "把 min_level 恢复成 Info 必须成功，否则打开级别指标不成立");
                static_cast<void>(ID::Engine::shutdown());
                return;
            }

            EnabledShortProbe short_probe{ logger, 0 };
            std::snprintf(note, sizeof(note), "%s；payload≈33B（<250 内联容量）：调用侧 "
                          "std::format_to_n + [category] 拼装 + async 入队；队列满按 overrun_drop_oldest 丢弃，"
                          "测得的是生产者每次调用代价，不是交付延迟/吞吐；console sink 写 stderr，且本次 stderr=file"
                          "（worker 线程侧成本不在本条内）",
                          note_prefix);
            record("log.enabled.short.ns", measure(short_probe), kReferences.enabled_short_ns, "", note);

            EnabledLongProbe long_probe{ logger, 0 };
            std::snprintf(note, sizeof(note), "%s；payload≈305B（>250 内联容量，log_msg_buffer 必然扩容）："
                          "其余同上；丢弃策略与短消息相同，不统计投递条数；stderr=file",
                          note_prefix);
            record("log.enabled.long.ns", measure(long_probe), kReferences.enabled_long_ns, "", note);
        }

        // ---------- 打开级别：同步形态（reconfigure async=false，同步形态不校验 queue_size） ----------
        {
            ID::Log::LogConfig sync_config{};
            sync_config.async      = false;
            sync_config.queue_size = 0;
            if (!table->reconfigure(sync_config))
            {
                Check(false, "perf.log.reconfigure_sync",
                      "同步形态的 reconfigure 必须成功，否则同步指标不成立");
                static_cast<void>(ID::Engine::shutdown());
                return;
            }

            EnabledShortProbe sync_probe{ logger, 0 };
            std::snprintf(note, sizeof(note), "%s；payload≈33B、async=false（reconfigure 重建后端）："
                          "写入在生产者线程直接进 console sink；**本次运行 stderr=file**，sink 写的是磁盘文件句柄"
                          "（CRT stderr 无缓冲 → 每行一次写系统调用），不是控制台成本，也不代表其它 sink 的成本；"
                          "一次调用 = 调用侧格式化 + 跨 DLL 一次间接调用 + 同步 sink 写，与 async 指标不可直接比较",
                          note_prefix);
            record("log.enabled.sync.ns", measure(sync_probe), kReferences.enabled_sync_ns, kSyncUngradedReason, note);

            ID::Log::LogConfig async_config{};
            if (!table->reconfigure(async_config))
            {
                Check(false, "perf.log.reconfigure_async", "回到默认 async 形态必须成功");
                static_cast<void>(ID::Engine::shutdown());
                return;
            }
        }

        // 冷路径不写官方 perf 行：reconfigure / 轮转重建每次都是"新建后端 + 销毁旧后端（join worker）"，
        // 1 次调用 = 1 个 op，实测 5 轮极差 40–80%，既不是 per-op 热路径也不能稳定评级。
        // 这里只声明口径，避免读者以为本域漏测；功能正确性由 ID.Log Security 的 log.reconfigure.* 覆盖
        Skip("log.reconfigure.cold_call",
             "冷路径重建后端（新建 sink/logger/thread_pool + 销毁旧后端时 join worker）不是 per-op 热路径，"
             "5 轮极差观测 40–80%，不写官方 perf 行、不参与评级；功能正确性见 ID.Log Security 的 log.reconfigure.*");
        Skip("log.overrun.drop_count",
             "rev2 已有 get_stats，但队列饱和丢弃计数需要先把 8192 容量填满并判断 overrun 语义，"
             "属独立压测；计数类证据不能塞进 ns 列");
        Skip("log.throughput",
             "吞吐/丢弃比例属于计数类证据，当前没有独立 raw 通道，不能换算后冒充 ns/op 测量");
        Skip("log.rotation.cold_call",
             "max_file_bytes/max_files 轮转重建同样是冷路径（新建 rotating sink + 销毁旧后端），"
             "与 reconfigure 冷路径同类，不写官方 perf 行；轮转正确性由 ID.Log Security 的 log.rotation.* 覆盖");

        // 计时结束后把在途消息交给产品自己的排空路径，再关产品，避免 shutdown 与仍在生产的循环重叠
        table->flush();

        const ID::PluginRuntimeDiagnostic stopped = ID::Engine::shutdown();
        if (!stopped.is_ok())
        {
            Check(false, "perf.log.engine.shutdown", "性能段结束后关闭必须成功并回到 Idle");
        }
    }
} // namespace ID::Bench

#else

namespace ID::Bench
{
    void IDLogPerformanceTest() noexcept
    {
        // 域必须始终有实现：未选中 ID.Log 的 profile 下不产生任何 DUT 访问，只说明为什么没测
        Skip("ID.Log", "product profile does not select ID.Log");
    }
} // namespace ID::Bench

#endif
