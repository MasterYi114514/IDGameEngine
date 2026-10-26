#include "IDLog.hpp"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "benchmark_support.hpp"

/**
 *  IDLog 性能用例。
 *  
 *  纪律（与 methodology / grading 一致）：
 *  - 每项"多轮"指标先预热（不计时），再固定 5 轮，报告**中位数**，并把最小/最大/极差写进 note；
 *    极差超过 20% 标 unstable，且按 grading.md §6 不当作有效评级：该项 basis 留空 → ungraded 并计入退出码。
 *  - 单轮总时长按本机 Release 实测读数标定到 ≥ 100 ms（methodology §2），实际值由 note 的
 *    "单轮≈X ms" 逐条披露；标定用的是本变体上一次运行的中位耗时，换机器/换配置需重新标定。
 *  - 固定输入：消息格式串与参数固定（见报告头 config 行），迭代数固定，不随机。
 *  - 防优化：日志写入有真实 I/O 副作用；登记路径用 KeepAlive 把返回值写进跨 TU sink。
 *  - 计时区间内不做 Check、不打印、不分配（延迟样本数组预分配）。
 *  - 参考值：与推导场景同类的"每条消息"操作（写日志、格式化、裁剪判断）以场景预算目标 500 ns/条作为
 *    reference，basis 取 `budget`（grading.md §2 E，POLICY 类；推导与全部假设见 measurement notes 的
 *    reference 推导节）。该值是"每帧留给日志的时间"这一工程预算，不是执行路径下界、不是硬件规格、也不是
 *    同轮实测对照；把它标成 theoretical 等于把 POLICY 包装成 DERIVED（grading.md §1 红线）。
 *    比值 < 0.5 只表示离预算还有余量，不触发"参考值过松"复核（该复核只适用于 theoretical 下界）。
 *  - 不适用预算的操作不借用该参考值：`get_logger` 登记是查表、不是"每条消息"操作，且没有同轮直接调用
 *    对照，因此 basis 留空 → ungraded（grading.md §2 E 适用范围），实测值只作登记成本记录。
 *  - console 目标在计时窗口内把 stdout 指向 NUL：既不淹没报告输出，也不把终端渲染成本算成库成本。
 *  - 延迟三项是"一次采样 pass 的分位数估计"，不是 5 轮中位数，报告头与 note 分别表述；其中 p90 的跨进程
 *    极差超出 grading.md §5 的 20% 噪声带（24.1%，见归档 repeat-runs.txt）、p99 在多组 5 连跑中全部超带，
 *    两项都 basis 留空、只做数量级比较；跨进程资格尚未进入代码自动判定（限制写进 measurement notes）。
 */

namespace
{
    using ID::Log::Level;
    using ID::Log::Logger;
    using ID::Log::LoggerHandle;
    using ID::Log::SinkType;

    constexpr int           kRounds         = 5;
    constexpr double        kMinRoundMs     = 100.0;        // methodology §2 的单轮时长下限

    // ---- 场景基准：每帧日志预算摊到单条的目标值（POLICY，不是下界） ----
    //
    // 推导：日志负载 20,000 条/秒 ÷ 60 fps ≈ 333.3 条/帧；每帧日志时间预算 = 16.67 ms × 1% ≈ 166.7 µs；
    //       166.7 µs ÷ 333.3 条 ≈ 500 ns/条。
    // 假设（缺一即不成立）：负载 20000 条/秒、60 fps（帧 16.67 ms）、日志预算占每帧 1%、消息约 40 B；
    //       构建身份 MinGW GCC 16.2.0 / Release / SHARED / x86-64(AMD64)。
    // 口径：该值描述"每帧留给日志多少时间"，与实现快慢无关，因此不是 theoretical 定义的理论下界，
    //       也不是 hardware 规格或同轮对照；把它标成 theoretical 等于把 POLICY 包装成 DERIVED
    //       （grading.md §1 红线）。grading.md §2 E 把场景预算目标定为第五类合法 basis `budget`，
    //       报告依据列显示 `<值> [budget]`，六档带宽照常按 ratio = actual / 500 ns 分档。
    constexpr double           kBudgetRefNs = 500.0;
    constexpr std::string_view kBudgetBasis = "budget";     // 必须是 string_view：空 const char* 构造
                                                            // string_view 会 strlen(nullptr)

    // note 尾巴：让报告读者看到参考值本身、其依据类别与口径出处
    constexpr const char*   kBudgetNoteTail =
        "；reference=500.00 ns/条 [budget]（场景预算目标 = POLICY，不是下界也不是同轮实测对照；"
        "推导与全部假设见 measurement notes 的 reference 推导节，口径见 grading.md §2 E；"
        "比值 < 0.5 只表示离预算还有余量，不触发参考值过松复核）";

    // 进程内极差超带时的 note 尾巴：不评级，只给数量级结论
    constexpr const char*   kUnstableNoteTail =
        "；reference=500.00 ns/条，但本项不给比值评级：进程内 5 轮极差超出 grading.md §5 的 20% 噪声带 → "
        "标 unstable、basis 留空 → ungraded 并计入退出码（grading.md §6：不把 unstable 当有效评级），"
        "只做数量级比较";

    // ---- 尾部延迟（p90 / p99）不参与比值评级 ----
    //
    // 依据：同一 Release 变体连续 5 次运行记录逐指标极差（grading.md §5 的首次稳定性确认）。
    //       p99 是"单次采样 pass 的尾部分位估计"，极差在多组独立 5 连跑中全部远超 20% 噪声带；
    //       p90 在上一块的跨进程极差为 24.1%（min 650.00 / max 812.50 / median 675.00 ns），同样超带。
    //       这两个分位的 pass 间长尾受文件写阻塞与机器后台负载支配（含首次运行的可执行文件冷启动/扫描开销），
    //       逐次读数与极差表在本变体归档 repeat-runs.txt。按 §5/§6 标 unstable、只做数量级比较，因此两项
    //       basis 留空 → ungraded 并计入退出码；换机器、换配置或换测量方法必须重新做 5 连跑确认，
    //       不得借用本机的稳定性结论。跨进程资格进入代码自动判定的限制写在 measurement notes，
    //       本文件不以注释占位代替该缺口。
    constexpr std::string_view kTailLatencyBasis = {};
    constexpr const char*   kTailLatencyNoteTail =
        "；reference=500.00 ns/条，但该项不给比值评级：单次采样 pass 的尾部分位在连续 5 次运行中的极差超出 "
        "grading.md §5 的 20% 噪声带 → 标 unstable，只做数量级比较（逐次读数与极差表见归档 repeat-runs.txt）；"
        "该项与 500 ns/条的预算处在同一数量级（实测为预算的个位数倍数以内），具体倍数不作为等级结论";
    constexpr const char*   kP90LatencyNoteTail =
        "；reference=500.00 ns/条，但该项本轮不给比值评级：上一块同变体跨进程 5 连跑的极差为 24.1%"
        "（min 650.00 / max 812.50 / median 675.00 ns），超出 grading.md §5 的 20% 噪声带 → 按 unstable 处理、"
        "basis 留空 → ungraded 并计入退出码，只做数量级比较；跨进程资格尚未进入代码自动判定"
        "（限制见 measurement notes 与 framework.md §4），逐次读数与极差表见归档 repeat-runs.txt";

    // ---- 登记路径不借用场景预算（grading.md §2 E 的适用范围） ----
    //
    // get_logger 是登记（查表）操作，不属于"每条消息"场景，且本配置没有同轮、同进程的直接调用对照，
    // 因此不给参考值：basis 留空 → ungraded 并计入退出码；实测值只作登记成本记录。
    constexpr const char*   kRegistrationNoteTail =
        "；本项 basis 留空 → ungraded：budget 不适用于登记路径（grading.md §2 E 的适用范围只限与推导场景同类的"
        "“每条消息”操作），且本配置没有同轮、同进程的直接调用对照；实测值仅作登记成本记录，不用于评级";

    // 迭代数标定（本机 Release + 上次实测中位耗时 → 单轮时长 ≥ 100 ms）
    constexpr std::size_t   kIoIters        = 700'000;      // console ≈185 ns、file ≈227 ns → 130–160 ms/轮
    constexpr std::size_t   kIoWarmup       = 100'000;
    constexpr std::size_t   kTrimIters      = 45'000'000;   // 裁剪路径 ≈2.5 ns → ≈111 ms/轮
    constexpr std::size_t   kTrimWarmup     = 3'000'000;
    constexpr std::size_t   kRegIters       = 12'000'000;   // 登记路径 ≈10.5 ns → ≈125 ms/轮
    constexpr std::size_t   kRegWarmup      = 2'000'000;
    constexpr std::size_t   kLatencySamples = 100'000;      // ×8 条/段 = 800k 条 → ≈120 ms 采样窗口
    constexpr std::size_t   kMtPerThread    = 500'000;      // 每线程 wall ≈110 ms（1 线程基准档）

    void RemoveFileQuiet(const std::string& path) noexcept
    {
        std::error_code error;
        std::filesystem::remove(path, error);
    }

    // 记录一条"每次操作"指标：actual 为每操作中位耗时；note 带吞吐、极值与单轮时长。
    // no_basis_note_tail 非空表示本项不给参考值（basis 留空 → ungraded），该 tail 必须写明为什么没有依据；
    // 为空时按场景预算目标（grading.md §2 E）记录。
    void RecordPerOpMetric(std::string_view name, double* samples, std::size_t iters,
                           std::string_view extra_note,
                           std::string_view no_basis_note_tail = {}) noexcept
    {
        const ID::Bench::SampleStats stats = ID::Bench::Summarize(samples, kRounds);
        const double per_second = (stats.median > 0.0) ? (1.0e9 / stats.median) : 0.0;
        const double round_ms = stats.median * static_cast<double>(iters) / 1.0e6;

        std::string note = ID::Bench::FormatDouble(per_second, 0) + " 条/秒；中位/最小/最大="
                           + ID::Bench::FormatDouble(stats.median, 2) + "/"
                           + ID::Bench::FormatDouble(stats.minimum, 2) + "/"
                           + ID::Bench::FormatDouble(stats.maximum, 2) + " ns；迭代="
                           + std::to_string(iters) + "/轮×" + std::to_string(kRounds)
                           + " 轮（单轮≈" + ID::Bench::FormatDouble(round_ms, 1) + " ms）；极差="
                           + ID::Bench::FormatDouble(stats.spread * 100.0, 1) + "%";
        note += extra_note;
        if (!no_basis_note_tail.empty())
        {
            // 本项不借用预算：basis 留空 → ungraded；进程内极差只作稳定性记录
            if (stats.is_unstable)
            {
                note += "；unstable（本项本身不评级，进程内极差仅作稳定性记录）";
            }
            note += no_basis_note_tail;
        }
        else if (stats.is_unstable)
        {
            // grading.md §5/§6：极差超 20% 噪声带的项标 unstable 且不得当有效评级 → basis 留空、只有数量级结论
            note += "；unstable";
            note += kUnstableNoteTail;
        }
        else
        {
            note += kBudgetNoteTail;
        }

        if (round_ms < kMinRoundMs)
        {
            // 标定不足只降低样本质量，不改变契约判定：如实告警并给出实测值
            std::string warn_name = std::string(name) + ".round_duration";
            ID::Bench::Warn(warn_name,
                            "单轮 " + ID::Bench::FormatDouble(round_ms, 1)
                                + " ms 低于 methodology §2 的 100 ms 下限：需提高迭代数后重测");
        }

        const bool has_reference = no_basis_note_tail.empty() && !stats.is_unstable;
        ID::Bench::RecordMetric({
            .name         = name,
            .actual_ns    = stats.median,
            .reference_ns = has_reference ? kBudgetRefNs : 0.0,
            .basis        = has_reference ? kBudgetBasis : std::string_view{},
            .note         = note
        });
    }

    // 单线程：固定迭代数写同一条消息，逐轮取每操作耗时
    void MeasureSingleThread(Logger* logger, std::size_t warmup_iters, std::size_t iters,
                             double* samples_out) noexcept
    {
        for (std::size_t index = 0; index < warmup_iters; ++index)
        {
            logger->info("idlog benchmark payload {}", index);
        }

        for (int round = 0; round < kRounds; ++round)
        {
            const ID::Bench::Timer timer;
            for (std::size_t index = 0; index < iters; ++index)
            {
                logger->info("idlog benchmark payload {}", index);
            }
            samples_out[round] = timer.ElapsedNs() / static_cast<double>(iters);
        }
    }

    // 多线程：全部线程就绪后统一开表，聚合口径为 总耗时 / (线程数 × 每线程条数)
    // 绑核失败只影响噪声：由 unpin_failures 累计，回到主线程后记一条告警
    void MeasureMultiThread(Logger* logger, int thread_count, std::size_t per_thread,
                            double* samples_out, std::atomic<int>& unpin_failures) noexcept
    {
        for (std::size_t index = 0; index < kIoWarmup; ++index)
        {
            logger->info("idlog benchmark payload {}", index);
        }

        for (int round = 0; round < kRounds; ++round)
        {
            std::atomic<int>  ready{ 0 };
            std::atomic<bool> start{ false };

            std::vector<std::thread> threads;
            threads.reserve(static_cast<std::size_t>(thread_count));
            for (int index = 0; index < thread_count; ++index)
            {
                threads.emplace_back([logger, per_thread, &ready, &start, &unpin_failures, index]() noexcept {
                    if (!ID::Bench::SetAffinity(index))     // 每线程绑一个逻辑核，减少迁移噪声
                    {
                        unpin_failures.fetch_add(1, std::memory_order_relaxed);
                    }
                    ready.fetch_add(1, std::memory_order_release);
                    while (!start.load(std::memory_order_acquire))
                    {
                        std::this_thread::yield();
                    }
                    for (std::size_t message = 0; message < per_thread; ++message)
                    {
                        logger->info("idlog benchmark payload {}", message);
                    }
                });
            }

            while (ready.load(std::memory_order_acquire) < thread_count)
            {
                std::this_thread::yield();
            }

            const ID::Bench::Timer timer;       // 线程创建与就绪等待都在计时之外
            start.store(true, std::memory_order_release);
            for (std::thread& thread : threads)
            {
                thread.join();
            }

            const std::size_t total = static_cast<std::size_t>(thread_count) * per_thread;
            samples_out[round] = timer.ElapsedNs() / static_cast<double>(total);
        }
    }
} // namespace

namespace ID::Bench
{
    void IdLogPerformanceTest() noexcept
    {
        ID::Log::init();

        const LoggerHandle handle = ID::Log::create_logger("perf");
        Logger* logger = ID::Log::get_logger(handle);
        if (logger == nullptr)
        {
            Check(false, "perf.setup", "性能域无法创建日志器，全部指标未测量");
            ID::Log::shutdown();
            return;
        }

        // ---- 1. 终端吞吐 ----
        {
            logger->disable_sink(SinkType::File);
            logger->enable_sink(SinkType::Console);
            logger->set_sink_level(SinkType::Console, Level::Trace);

            StdoutRedirect redirect;
            if (!redirect.Begin("NUL"))
            {
                Check(false, "perf.console_stdout_silenced",
                      "stdout 无法重定向到 NUL：终端渲染会污染报告输出，console 吞吐未测量");
            }
            else
            {
                double samples[kRounds] = {};
                MeasureSingleThread(logger, kIoWarmup, kIoIters, samples);
                redirect.End();

                RecordPerOpMetric("idlog.console.throughput", samples, kIoIters,
                                  "；sink=stdout_color_sink_mt；单线程；计时窗口内 stdout→NUL（不含终端渲染成本）；"
                                  "写入路径在消费者 TU 内实例化（header-only）");
            }
        }

        // ---- 2. 文件吞吐（单线程，作为多线程扩展性的基准） ----
        const std::string file_path = TempPath("perf_file.log");
        RemoveFileQuiet(file_path);

        logger->disable_sink(SinkType::Console);
        logger->set_sink_level(SinkType::File, Level::Trace);
        if (!logger->set_sink_path(SinkType::File, file_path.c_str()))
        {
            Check(false, "perf.file_target",
                  "文件目标启用失败：文件吞吐、延迟、多线程与裁剪放行档均未测量");
            logger->disable_sink(SinkType::File);
            ID::Log::destroy_logger(handle);
            ID::Log::shutdown();
            return;
        }

        double file_samples[kRounds] = {};
        MeasureSingleThread(logger, kIoWarmup, kIoIters, file_samples);
        RecordPerOpMetric("idlog.file.throughput", file_samples, kIoIters,
                          "；sink=basic_file_sink_mt(append)；单线程；终端目标已关闭");

        // ---- 3. 单条延迟分位数（同一文件目标；一次采样 pass，非 5 轮中位数） ----
        {
            // 每个样本连续写 kLatencyBatch 条后除以条数：本机计时分辨率为 100 ns，
            // 直接单条计时会把 100 ns 的量化误差放大到中位数的 10% 以上；分 8 条可摊到 12.5 ns/条。
            constexpr std::size_t kLatencyBatch = 8;

            std::vector<double> latencies(kLatencySamples, 0.0);
            for (std::size_t index = 0; index < kIoWarmup; ++index)
            {
                logger->info("idlog benchmark payload {}", index);
            }

            const double pass_begin = NowNs();
            for (std::size_t sample = 0; sample < kLatencySamples; ++sample)
            {
                const double begin = NowNs();
                for (std::size_t inner = 0; inner < kLatencyBatch; ++inner)
                {
                    logger->info("idlog benchmark payload {}", inner);
                }
                const double end = NowNs();
                latencies[sample] = (end - begin) / static_cast<double>(kLatencyBatch);
            }
            const double pass_ms = (NowNs() - pass_begin) / 1.0e6;

            std::sort(latencies.begin(), latencies.end());
            const double p50 = PercentileOfSorted(latencies.data(), kLatencySamples, 0.50);
            const double p90 = PercentileOfSorted(latencies.data(), kLatencySamples, 0.90);
            const double p99 = PercentileOfSorted(latencies.data(), kLatencySamples, 0.99);

            if (pass_ms < kMinRoundMs)
            {
                Warn("idlog.file.latency.pass_duration",
                     "采样窗口 " + FormatDouble(pass_ms, 1)
                         + " ms 低于 methodology §2 的 100 ms 下限：需提高样本数后重测");
            }

            const std::string note_base = "单次采样 pass 的分位数估计（非 5 轮中位数）；样本="
                                          + std::to_string(kLatencySamples) + " 段×"
                                          + std::to_string(kLatencyBatch) + " 条（共 "
                                          + std::to_string(kLatencySamples * kLatencyBatch)
                                          + " 条，采样窗口≈" + FormatDouble(pass_ms, 1) + " ms）；"
                                          "每段值=段耗时/" + std::to_string(kLatencyBatch)
                                          + "（把 100 ns 计时分辨率摊到 12.5 ns/条，代价是单条长尾被段内平均）；"
                                          "计时源=" + TimerSourceName()
                                          + " 分辨率=" + FormatDouble(TimerResolutionNs(), 1)
                                          + " ns；同轮分位 p90=" + FormatDouble(p90, 2)
                                          + " ns、p99=" + FormatDouble(p99, 2)
                                          + " ns；sink=basic_file_sink_mt；单线程";
            const std::string note        = note_base + kBudgetNoteTail;
            const std::string p90_note    = note_base + kP90LatencyNoteTail;
            const std::string p99_note    = note_base + kTailLatencyNoteTail;

            RecordMetric({
                .name         = "idlog.file.latency_p50",
                .actual_ns    = p50,
                .reference_ns = kBudgetRefNs,
                .basis        = kBudgetBasis,
                .note         = note
            });
            RecordMetric({
                .name         = "idlog.file.latency_p90",
                .actual_ns    = p90,
                .reference_ns = kBudgetRefNs,
                .basis        = kTailLatencyBasis,      // 空：跨进程 5 连跑极差 24.1% 超 20% 噪声带 → 不评级
                .note         = p90_note
            });
            RecordMetric({
                .name         = "idlog.file.latency_p99",
                .actual_ns    = p99,
                .reference_ns = kBudgetRefNs,
                .basis        = kTailLatencyBasis,      // 空：多组 5 连跑极差全部超 20% 噪声带 → 不评级
                .note         = p99_note
            });
        }

        // ---- 4. 多线程竞争（1 / 2 / 4 线程，同一 logger 同一文件目标） ----
        {
            std::atomic<int> unpin_failures{ 0 };

            double samples_1[kRounds] = {};
            double samples_2[kRounds] = {};
            double samples_4[kRounds] = {};

            MeasureMultiThread(logger, 1, kMtPerThread, samples_1, unpin_failures);
            MeasureMultiThread(logger, 2, kMtPerThread, samples_2, unpin_failures);
            MeasureMultiThread(logger, 4, kMtPerThread, samples_4, unpin_failures);

            const int unpinned_failed = unpin_failures.load(std::memory_order_relaxed);
            if (unpinned_failed > 0)
            {
                Warn("idlog.mt.affinity_pin_failed",
                     std::to_string(unpinned_failed) + " 次 SetAffinity(core) 返回 false；"
                     "线程可能迁移，多线程样本含迁移噪声，不影响正确性判定");
            }

            const double median_1 = Summarize(samples_1, kRounds).median;
            const double median_2 = Summarize(samples_2, kRounds).median;
            const double median_4 = Summarize(samples_4, kRounds).median;

            const std::string thread_note = "；每线程 " + std::to_string(kMtPerThread)
                                            + " 条；同 logger 同文件目标";
            RecordPerOpMetric("idlog.mt.file.1thread", samples_1, kMtPerThread,
                              thread_note + "；线程=1（扩展性基准）");
            RecordPerOpMetric("idlog.mt.file.2threads", samples_2, kMtPerThread * 2,
                              thread_note + "；线程=2（各绑一核）；相对 1 线程扩展性="
                                  + FormatDouble((median_2 > 0.0) ? (median_1 / median_2) : 0.0, 2)
                                  + "×（理想 2.00×）");
            RecordPerOpMetric("idlog.mt.file.4threads", samples_4, kMtPerThread * 4,
                              thread_note + "；线程=4（各绑一核）；相对 1 线程扩展性="
                                  + FormatDouble((median_4 > 0.0) ? (median_1 / median_4) : 0.0, 2)
                                  + "×（理想 4.00×）");
        }

        // ---- 5. 被裁剪消息的成本：全部目标裁剪 vs 部分裁剪 ----
        {
            // 两档都保持"终端目标已启用但阈值 Fatal"，因此终端 sink 参与阈值最小值计算但不产生输出
            logger->enable_sink(SinkType::Console);
            logger->set_sink_level(SinkType::Console, Level::Fatal);

            logger->set_sink_level(SinkType::File, Level::Fatal);
            double samples_all[kRounds] = {};
            MeasureSingleThread(logger, kTrimWarmup, kTrimIters, samples_all);
            RecordPerOpMetric("idlog.trim.all_targets", samples_all, kTrimIters,
                              "；console=Fatal 且 file=Fatal，写 Info：后端阈值取已启用目标最小值 → 格式化前丢弃，"
                              "不格式化、不分配、不阻塞");

            logger->set_sink_level(SinkType::File, Level::Trace);
            double samples_partial[kRounds] = {};
            MeasureSingleThread(logger, kIoWarmup, kIoIters, samples_partial);
            RecordPerOpMetric("idlog.trim.partial_one_target", samples_partial, kIoIters,
                              "；console=Fatal（丢弃）+ file=Trace（放行）：格式化一次 + 单目标写文件");
        }

        // ---- 6. get_logger 登记路径成本（接入成本的代表，见 measurement notes） ----
        {
            for (std::size_t index = 0; index < kRegWarmup; ++index)
            {
                KeepAlive(ID::Log::get_logger(handle));
            }

            double samples[kRounds] = {};
            for (int round = 0; round < kRounds; ++round)
            {
                Logger* last = nullptr;
                const Timer timer;
                for (std::size_t index = 0; index < kRegIters; ++index)
                {
                    last = ID::Log::get_logger(handle);
                }
                samples[round] = timer.ElapsedNs() / static_cast<double>(kRegIters);
                KeepAlive(last);
            }

            RecordPerOpMetric("idlog.get_logger.registration", samples, kRegIters,
                              "；跨 DLL 导出的登记路径：一次 mutex + 槽位存活判定；这是登记（查表）成本，"
                              "不是日志写入成本，也不能替代同轮直接调用对照的接入增量差值",
                              kRegistrationNoteTail);
        }

        // ---- 收尾 ----
        logger->disable_sink(SinkType::File);
        ID::Log::destroy_logger(handle);
        ID::Log::shutdown();
        RemoveFileQuiet(file_path);
    }

    void IdLogPerformanceNotes() noexcept
    {
#if !defined(NDEBUG)
        ReportLine("    - 变体资格（provisional）：本报告来自未定义 NDEBUG 的构建（Debug 变体），"
                   "未做 grading.md §5 要求的跨进程 5 连跑资格确认，仅作参考归档；"
                   "其中的等级与读数不代表 Release 官方结论");
#endif
        ReportLine("    - 统计口径：多轮指标（throughput / trim / registration / mt）为预热 + 5 轮、报告中位数，"
                   "最小/最大/极差见各 note；延迟三项为单次采样 pass 的分位数估计，不做多轮中位数");
        ReportLine("    - 证据可比性：本报告的用例标识符为契约语义名（如 file.path_enable_usable、sink.invalid_type_*），"
                   "配置接口为 enable_sink / disable_sink / set_sink_level / set_sink_path；与旧接口时代的报告或归档"
                   "（用例名 enable_file.* / console.* 那一版）不可直接逐行对比，只有换算后的指标数值可作参考");
        ReportLine("    - 未列入指标：init() / shutdown() 的成本（methodology §7 允许把初始化与清理单独说明而不混入"
                   "热路径读数）；本轮没有单独测量二者，不给出任何数字");
        ReportLine("    - 接入成本口径：get_logger 登记路径（跨 DLL 导出 + mutex）只作为登记成本记录，"
                   "本轮 basis 留空 → ungraded：budget 不适用于登记/查表这类没有场景假设支撑的操作"
                   "（grading.md §2 E 的适用范围），且本配置没有同轮、同进程的“直接调用对照”，"
                   "因此既不构成接入增量差值，也不借用 500 ns/条的预算参考值");
        ReportLine("    - reference 推导（场景预算目标 = POLICY，不是理论下界）：20000 条/秒 ÷ 60 fps ≈ 333.3 条/帧；"
                   "每帧日志时间预算 = 16.67 ms × 1% ≈ 166.7 µs；166.7 µs ÷ 333.3 条 ≈ 500 ns/条");
        ReportLine("      假设（缺一即不成立）：日志负载 20000 条/秒（用户给定）；60 fps（帧 16.67 ms）；"
                   "日志时间预算占每帧 1%；单条消息约 40 B；构建身份 MinGW GCC 16.2.0 / Release / SHARED / AMD64");
        ReportLine("      依据类别：稳定项的 basis 为 budget（grading.md §2 E 的场景预算目标，POLICY 类）：它是目标，"
                   "不是理论下界、不是硬件规格、也不是同轮实测对照；ratio = actual ÷ 500 ns 后按 grading.md §5 的"
                   "六档带宽评级，比值 < 0.5 只表示离预算还有余量，不触发“参考值过松”复核（该复核仅适用于 theoretical）");
        ReportLine("      适用范围（grading.md §2 E）：budget 只用于与推导场景同类的“每条消息”操作"
                   "（console/file 吞吐、延迟分位、裁剪判断、多线程写文件）；get_logger 登记不适用，basis 留空 →"
                   " ungraded 并计入退出码");
        ReportLine("      噪声带确认（grading.md §5）：±20% 带宽需在该机器/配置上首次连续 5 次运行并记录逐指标极差；"
                   "跨进程逐次读数与极差表见归档 <benchmark>/build/gcc16.2-release-shared/repeat-runs.txt，"
                   "本进程内的 5 轮极差见各指标 note");
        ReportLine("      跨进程资格的限制（framework.md §4）：同机同配置连续 5 次运行与逐指标极差目前只能由运行者手工连跑并"
                   "把读数归档（repeat-runs.txt），尚未进入代码自动判定；因此由 5 连跑结论驱动的不评级项"
                   "（idlog.file.latency_p90、idlog.file.latency_p99）是按归档证据手工标注的，单次运行的等级只代表该次运行，"
                   "不能当作已交叉运行确认的结论");
        ReportLine("      不稳定项处置（grading.md §5/§6）：多轮指标的本进程 5 轮极差 > 20% 时自动标 unstable、basis 留空"
                   " → ungraded 并计入退出码，只做数量级比较；延迟 p90 / p99 是单次采样 pass 的尾部分位，"
                   "跨进程极差同样超出该带（受文件写阻塞与后台负载影响），固定标 unstable 不给比值评级");
    }
} // namespace ID::Bench
