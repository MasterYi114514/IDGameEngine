#include "benchmark_support.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
    #if !defined(WIN32_LEAN_AND_MEAN)
        #define WIN32_LEAN_AND_MEAN
    #endif
    #if !defined(NOMINMAX)
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <fcntl.h>
    #include <io.h>
    #include <sys/stat.h>
#else
    #include <fcntl.h>
    #include <unistd.h>
#endif

/**
 *  公共设施实现。
 *  
 *  设计取舍：
 *  - 会话状态放在函数局部 static 里（首次使用时构造），避免运行期全局构造函数与静态析构顺序问题。
 *  - 输出只有一条路径：`Emit` 同时写 stdout 与报告文件；报告写失败只关掉报告通道并置退出码位，
 *    不影响后续用例执行与取证。
 *  - 评级在这一层集中完成：用例只提供五字段，等级与比值由本层按 grading 口径计算，
 *    非法依据 / 非正数值一律 ungraded 并计入失败。
 */

namespace
{
    constexpr std::size_t kNameColumnWidth     = 44;    // 用例名左对齐宽度
    constexpr std::size_t kMetricNameWidth     = 34;    // 指标名左对齐宽度
    constexpr double      kUnstableSpreadRatio = 0.20;  // 极差超过中位数 20% 视为 unstable

    struct MetricRecord
    {
        std::string m_name;
        std::string m_basis;
        std::string m_note;
        std::string m_grade;
        double      m_actual      = 0.0;
        double      m_reference   = 0.0;
        double      m_ratio       = 0.0;
        bool        m_has_ratio   = false;
    };

    struct Session
    {
        std::ofstream               m_report;
        std::string                 m_report_path;
        std::vector<MetricRecord>   m_metrics;

        std::size_t                 m_domain_count          = 0;
        std::size_t                 m_passed                = 0;
        std::size_t                 m_case_failed           = 0;    // 只有 Check(false) 计入（两个区都算）
        std::size_t                 m_skipped               = 0;
        std::size_t                 m_uncovered             = 0;
        std::size_t                 m_warnings              = 0;
        std::size_t                 m_domain_passed         = 0;    // 当前分区的计数
        std::size_t                 m_domain_failed         = 0;
        std::size_t                 m_domain_skipped        = 0;
        std::size_t                 m_grade_counts[6]       = {};
        std::size_t                 m_ungraded              = 0;

        bool                        m_in_performance        = false; // 当前分区是否在性能区
        bool                        m_report_ok             = false; // 会话开始时报告是否成功打开
        bool                        m_report_open           = false; // 当前是否仍可写
        bool                        m_report_failed         = false;
        bool                        m_begun                 = false;
    };

    Session& State() noexcept
    {
        static Session s_session;
        return s_session;
    }

    // 合法 basis 白名单：与 grading.md §2 的五类来源逐值一致
    // （theoretical / hardware / os-baseline / regression / budget；budget 为场景预算目标，POLICY 类）
    bool IsLegalBasis(std::string_view basis) noexcept
    {
        return basis == "theoretical" || basis == "hardware" || basis == "os-baseline" || basis == "regression"
            || basis == "budget";
    }

    // ---- 计时源（Windows: QueryPerformanceCounter） ----

    std::int64_t ClockFrequency() noexcept
    {
#if defined(_WIN32)
        static const std::int64_t s_frequency = []() noexcept -> std::int64_t {
            LARGE_INTEGER value{};
            ::QueryPerformanceFrequency(&value);
            return static_cast<std::int64_t>(value.QuadPart);
        }();
        return s_frequency;
#else
        return 1000000000LL;
#endif
    }

    std::int64_t ClockTicks() noexcept
    {
#if defined(_WIN32)
        LARGE_INTEGER value{};
        ::QueryPerformanceCounter(&value);
        return static_cast<std::int64_t>(value.QuadPart);
#else
        return static_cast<std::int64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
#endif
    }

    double TicksToNs(std::int64_t ticks) noexcept
    {
        const std::int64_t frequency = ClockFrequency();
        if (frequency <= 0)
        {
            return 0.0;
        }

        return static_cast<double>(ticks) * 1.0e9 / static_cast<double>(frequency);
    }

    // 探测最小可分辨步长：连续读取的最小正差值，与 1e9/频率 取较大者
    double ProbeResolutionNs() noexcept
    {
        const double nominal = (ClockFrequency() > 0) ? (1.0e9 / static_cast<double>(ClockFrequency())) : 0.0;

        std::int64_t smallest = 0;
        for (int probe = 0; probe < 64; ++probe)
        {
            const std::int64_t begin = ClockTicks();
            std::int64_t delta       = 0;
            for (int spin = 0; spin < 16; ++spin)
            {
                delta = ClockTicks() - begin;
                if (delta > 0)
                {
                    break;
                }
            }

            if (delta > 0 && (smallest == 0 || delta < smallest))
            {
                smallest = delta;
            }
        }

        const double measured = TicksToNs(smallest);
        return (measured > nominal) ? measured : nominal;
    }

    double ResolutionNs() noexcept
    {
        static const double s_resolution = ProbeResolutionNs();
        return s_resolution;
    }

    const char* MachineIdentifier() noexcept
    {
#if defined(_WIN32)
        static const std::string s_machine = []() -> std::string {
            std::string identifier = "unknown";

            char computer_name[MAX_PATH] = {};
            DWORD name_length = static_cast<DWORD>(sizeof(computer_name));
            if (::GetComputerNameA(computer_name, &name_length) != FALSE)
            {
                identifier.assign(computer_name, name_length);
            }

            const char* processor = std::getenv("PROCESSOR_IDENTIFIER");
            if (processor != nullptr)
            {
                identifier += " | ";
                identifier += processor;
            }

            ::SYSTEM_INFO system_info{};
            ::GetSystemInfo(&system_info);
            identifier += " | logical=" + std::to_string(static_cast<unsigned>(system_info.dwNumberOfProcessors));
            return identifier;
        }();
        return s_machine.c_str();
#else
        // 非 Windows 平台未实现（未声称支持 Linux）：不猜机器标识，如实报告未实现
        return "unknown (non-Windows: machine identifier not implemented)";
#endif
    }

    #define ID_BENCH_STRINGIZE_IMPL(text) #text
    #define ID_BENCH_STRINGIZE(text)      ID_BENCH_STRINGIZE_IMPL(text)

    const char* CompilerIdentifier() noexcept
    {
#if defined(__GNUC__)
        return "GCC " __VERSION__ " (MinGW-w64)";
#elif defined(_MSC_VER)
        return "MSVC _MSC_VER=" ID_BENCH_STRINGIZE(_MSC_VER);
#else
        return "unknown";
#endif
    }

    std::string TimestampText() noexcept
    {
        const std::time_t now = std::time(nullptr);
        std::tm local{};
#if defined(_WIN32)
        localtime_s(&local, &now);
#else
        localtime_r(&now, &local);
#endif
        char text[32] = {};
        std::snprintf(text, sizeof(text), "%04d-%02d-%02d %02d:%02d:%02d",
                      local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                      local.tm_hour, local.tm_min, local.tm_sec);
        return std::string(text);
    }

    // ---- 输出：控制台与报告同源 ----

    void WriteAll(std::string_view text) noexcept
    {
        std::fwrite(text.data(), 1, text.size(), stdout);
        std::fputc('\n', stdout);
        std::fflush(stdout);

        Session& session = State();
        if (!session.m_report_open)
        {
            return;
        }

        session.m_report.write(text.data(), static_cast<std::streamsize>(text.size()));
        session.m_report.put('\n');
        session.m_report.flush();
        if (!session.m_report.good())
        {
            // 写失败后关闭报告通道：控制台仍然有效，退出码位 2 体现
            session.m_report_open = false;
            session.m_report_failed = true;
        }
    }

    void Emit(const std::string& text) noexcept
    {
        WriteAll(text);
    }

    // 依据不足一律 ungraded：缺 basis（传空）、basis 非法、缺 note、actual/reference 非正
    const char* GradeText(double actual, double reference, std::string_view basis, std::string_view note,
                          double& ratio_out, bool& has_ratio_out) noexcept
    {
        has_ratio_out = false;
        ratio_out = 0.0;

        if (!IsLegalBasis(basis) || note.empty() || actual <= 0.0 || reference <= 0.0)
        {
            return "ungraded";
        }

        const double ratio = actual / reference;
        ratio_out = ratio;
        has_ratio_out = true;

        if (ratio <= 0.50)
        {
            return "world-class";
        }
        if (ratio <= 0.80)
        {
            return "excellent";
        }
        if (ratio <= 1.20)
        {
            return "good";
        }
        if (ratio <= 2.00)
        {
            return "fair";
        }
        if (ratio <= 4.00)
        {
            return "poor";
        }
        return "terrible";
    }

    std::size_t& GradeSlot(Session& session, const std::string& grade) noexcept
    {
        static std::size_t s_never = 0;
        if (grade == "world-class") { return session.m_grade_counts[0]; }
        if (grade == "excellent")   { return session.m_grade_counts[1]; }
        if (grade == "good")        { return session.m_grade_counts[2]; }
        if (grade == "fair")        { return session.m_grade_counts[3]; }
        if (grade == "poor")        { return session.m_grade_counts[4]; }
        if (grade == "terrible")    { return session.m_grade_counts[5]; }
        return s_never;
    }
} // namespace

namespace ID::Bench
{
    // ---- 计时 ----

    Timer::Timer() noexcept
        : m_start_ticks(ClockTicks())
    {
    }

    double Timer::ElapsedNs() const noexcept
    {
        return TicksToNs(ClockTicks() - m_start_ticks);
    }

    double NowNs() noexcept
    {
        return TicksToNs(ClockTicks());
    }

    double TimerResolutionNs() noexcept
    {
        return ResolutionNs();
    }

    const char* TimerSourceName() noexcept
    {
#if defined(_WIN32)
        return "QueryPerformanceCounter";
#else
        return "std::chrono::steady_clock";
#endif
    }

    double TimerFrequencyHz() noexcept
    {
        return static_cast<double>(ClockFrequency());
    }

    // ---- 样本统计 ----

    SampleStats Summarize(double* samples, std::size_t count) noexcept
    {
        SampleStats stats;
        if (samples == nullptr || count == 0)
        {
            return stats;
        }

        std::sort(samples, samples + count);
        stats.minimum = samples[0];
        stats.maximum = samples[count - 1];
        stats.median = ((count % 2) == 1)
                           ? samples[count / 2]
                           : (0.5 * (samples[count / 2 - 1] + samples[count / 2]));
        stats.spread = (stats.median > 0.0) ? ((stats.maximum - stats.minimum) / stats.median) : 0.0;
        stats.is_unstable = stats.spread > kUnstableSpreadRatio;
        return stats;
    }

    double PercentileOfSorted(const double* sorted_samples, std::size_t count, double fraction) noexcept
    {
        if (sorted_samples == nullptr || count == 0)
        {
            return 0.0;
        }

        double clamped = fraction;
        if (clamped < 0.0)
        {
            clamped = 0.0;
        }
        if (clamped > 1.0)
        {
            clamped = 1.0;
        }

        std::size_t index = static_cast<std::size_t>(clamped * static_cast<double>(count - 1) + 0.5);
        if (index >= count)
        {
            index = count - 1;
        }
        return sorted_samples[index];
    }

    std::string FormatDouble(double value, int decimals) noexcept
    {
        int places = decimals;
        if (places < 0)
        {
            places = 0;
        }
        if (places > 9)
        {
            places = 9;
        }

        // 单值格式化：本机工具链（GCC 16.2.0 + UCRT）下一次传 11 个可变参数的 snprintf 会在写报告时
        // 触发访问冲突 0xC0000005（见 fixtures/toolchain-probe 与其日志），这里只传一个 double 规避
        char text[64] = {};
        std::snprintf(text, sizeof(text), "%.*f", places, value);
        return std::string(text);
    }

    std::string PadRight(std::string_view text, std::size_t width) noexcept
    {
        std::string padded(text);
        if (padded.size() < width)
        {
            padded.append(width - padded.size(), ' ');
        }
        return padded;
    }

    std::string PadLeft(std::string_view text, std::size_t width) noexcept
    {
        if (text.size() >= width)
        {
            return std::string(text);
        }
        std::string padded(width - text.size(), ' ');
        padded.append(text);
        return padded;
    }

    // ---- 防优化 sink ----

    void KeepAliveSink(const void* pointer) noexcept
    {
        // 指针本身是 volatile 的：写入属于可观测副作用，编译器不能删除，LTO 也无法把它消除；
        // 回读一次让 -Wunused-but-set-variable 满意（每批一次调用，不在计时区间内）。
        static const void* volatile s_sink = nullptr;
        s_sink = pointer;
        (void)s_sink;
    }

    // ---- 路径与线程 ----

    std::string TempPath(std::string_view name)
    {
        std::string path = BENCHMARK_TEMP_DIR;
        path += '/';
        path.append(name.data(), name.size());
        return path;
    }

    int LogicalCoreCount() noexcept
    {
#if defined(_WIN32)
        ::SYSTEM_INFO system_info{};
        ::GetSystemInfo(&system_info);
        return static_cast<int>(system_info.dwNumberOfProcessors);
#else
        return 1;
#endif
    }

    bool SetAffinity(int core) noexcept
    {
#if defined(_WIN32)
        if (core < 0)
        {
            const DWORD_PTR all = ~static_cast<DWORD_PTR>(0);
            return ::SetThreadAffinityMask(::GetCurrentThread(), all) != 0;
        }

        if (core >= LogicalCoreCount())
        {
            return false;
        }

        const DWORD_PTR mask = static_cast<DWORD_PTR>(1) << static_cast<unsigned>(core);
        return ::SetThreadAffinityMask(::GetCurrentThread(), mask) != 0;
#else
        (void)core;
        return false;
#endif
    }

    // ---- stdout 重定向 ----

    StdoutRedirect::~StdoutRedirect() noexcept
    {
        End();
    }

    bool StdoutRedirect::Begin(const char* target_path) noexcept
    {
#if defined(_WIN32)
        if (target_path == nullptr || m_saved_fd >= 0)
        {
            return false;
        }

        std::fflush(stdout);

        const int target_fd = _open(target_path, _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, _S_IREAD | _S_IWRITE);
        if (target_fd < 0)
        {
            return false;
        }

        const int saved_fd = _dup(_fileno(stdout));
        if (saved_fd < 0)
        {
            _close(target_fd);
            return false;
        }

        if (_dup2(target_fd, _fileno(stdout)) != 0)
        {
            _close(target_fd);
            _close(saved_fd);
            return false;
        }

        _close(target_fd);
        m_saved_fd = saved_fd;
        return true;
#else
        // POSIX 分支未实现（未声称支持 Linux）：恒定返回 false 且不改动任何状态，
        // 依赖它的可见性用例按"平台/环境不适用"记 Skip，不会污染真实控制台
        (void)target_path;
        return false;
#endif
    }

    void StdoutRedirect::End() noexcept
    {
#if defined(_WIN32)
        if (m_saved_fd < 0)
        {
            return;
        }

        std::fflush(stdout);
        _dup2(m_saved_fd, _fileno(stdout));
        _close(m_saved_fd);
        m_saved_fd = -1;
#endif
    }

    // ---- 会话 ----

    void BeginSession() noexcept
    {
        Session& session = State();
        if (session.m_begun)
        {
            return;
        }
        session.m_begun = true;

        std::error_code error;
        std::filesystem::create_directories(BENCHMARK_TEMP_DIR, error);

        session.m_report_path = BENCHMARK_REPORT_PATH;
        session.m_report.open(session.m_report_path,
                              std::ios::out | std::ios::trunc | std::ios::binary);
        session.m_report_open = session.m_report.is_open();
        session.m_report_ok = session.m_report_open;

        Emit(std::string("=== IDGameEngine Benchmark Report ==="));
        Emit("generated     : " + TimestampText());
        Emit(std::string("compiler      : ") + CompilerIdentifier() + " | arch=" + BENCHMARK_ARCH
             + " | bits=" + std::to_string(static_cast<unsigned>(sizeof(void*) * 8)));
        Emit(std::string("build         : ") + BENCHMARK_BUILD_CONFIG + " | LINK=" + BENCHMARK_LINK_MODE
             + " | LTO=" + BENCHMARK_LTO + " | ARCH=" + BENCHMARK_ARCH);
        {
            const bool pinned = SetAffinity(0);
            Emit(std::string("timer         : ") + TimerSourceName()
                 + " (freq=" + FormatDouble(TimerFrequencyHz(), 0)
                 + " Hz, resolution=" + FormatDouble(ResolutionNs(), 1)
                 + " ns) | affinity=" + (pinned ? "core 0" : "unpinned"));

            if (!pinned)
            {
                // 绑核失败不影响正确性判定，但样本可能含线程迁移噪声：如实记一条告警
                Warn("affinity.pin_failed",
                     "SetAffinity(0) 返回 false（逻辑核数=" + std::to_string(LogicalCoreCount())
                         + "）：线程可能迁移，样本含迁移噪声，不影响正确性判定");
            }
        }
        Emit(std::string("machine       : ") + MachineIdentifier());
        Emit("report        : " + session.m_report_path);
        Emit("temp dir      : " + std::string(BENCHMARK_TEMP_DIR));
        Emit("config        : message='idlog benchmark payload {}' (~40 B) | console=stdout_color_sink_mt"
             " | file=basic_file_sink_mt(append) | level=Fatal means critical");
        // 统计口径按指标类型分别表述：多轮取中位数与单次采样取分位数不是同一种估计
        Emit("measurement   : rounds metrics (throughput / trim / registration / mt) = warmup + 5 timed rounds,"
             " median of per-op ns reported, min/max/spread in note");
        Emit("measurement   : latency metrics (latency_p50/p90/p99) = one sampling pass over batched samples,"
             " percentile estimate of that pass; not a 5-round median");
        Emit("unverified    : MSVC (spdlog 1.17 内嵌 fmt 需 /utf-8，本工程未设置 -> 构建阻塞) | Linux (未构建)"
             " | real terminal rendering (console 计时期间 stdout->NUL，未测终端渲染)");
        WriteAll("--------------------------------------------------------------------");

        if (!session.m_report_open)
        {
            Emit("  [WARN] 报告文件无法写入（控制台仍然有效，退出码含 report-failure 位）");
        }
    }

    void Run(std::string_view domain, TestFn correctness, TestFn performance,
             TestFn performance_notes) noexcept
    {
        Session& session = State();
        ++session.m_domain_count;

        const std::string domain_title = "## " + std::string(domain) + " \xE2\x80\x94 ";

        // ---- 正确性 ----
        Emit("");
        Emit(domain_title + "Correctness");
        session.m_domain_passed = 0;
        session.m_domain_failed = 0;
        session.m_domain_skipped = 0;
        session.m_in_performance = false;
        const std::size_t uncovered_before = session.m_uncovered;
        if (correctness != nullptr)
        {
            correctness();
        }
        session.m_passed += session.m_domain_passed;
        session.m_case_failed += session.m_domain_failed;
        session.m_skipped += session.m_domain_skipped;
        Emit("  result: " + std::to_string(session.m_domain_passed) + " passed / "
             + std::to_string(session.m_domain_failed) + " failed / "
             + std::to_string(session.m_domain_skipped) + " skipped / "
             + std::to_string(session.m_uncovered - uncovered_before) + " uncovered");

        // ---- 性能 ----
        Emit("");
        Emit(domain_title + "Performance");

        // 性能区的 Check 用独立前缀就地输出（不与指标表混排），随后把计数并入全局失败与退出码
        session.m_domain_passed = 0;
        session.m_domain_failed = 0;
        session.m_domain_skipped = 0;
        session.m_in_performance = true;

        const std::size_t first_metric = session.m_metrics.size();
        const std::size_t perf_uncovered_before = session.m_uncovered;
        if (performance != nullptr)
        {
            performance();
        }
        session.m_in_performance = false;

        const std::size_t performance_passed    = session.m_domain_passed;
        const std::size_t performance_failed    = session.m_domain_failed;
        const std::size_t performance_skipped   = session.m_domain_skipped;
        const std::size_t performance_uncovered = session.m_uncovered - perf_uncovered_before;

        session.m_passed += performance_passed;
        session.m_case_failed += performance_failed;
        session.m_skipped += performance_skipped;

        Emit("  metric                              actual      reference            ratio  grade");
        Emit("  ---------------------------------------------------------------------------------");

        for (std::size_t index = first_metric; index < session.m_metrics.size(); ++index)
        {
            const MetricRecord& record = session.m_metrics[index];

            const std::string actual_text = FormatDouble(record.m_actual, 2) + " ns";
            // 没有可核实的依据时不显示参考数字，也不回显非法 basis：依据列写 "-"
            const std::string reference_text =
                record.m_has_ratio
                    ? (FormatDouble(record.m_reference, 2) + " ns [" + record.m_basis + "]")
                    : ("n/a [" + std::string(IsLegalBasis(record.m_basis) ? record.m_basis : std::string("-")) + "]");
            const std::string ratio_text = record.m_has_ratio ? FormatDouble(record.m_ratio, 2)
                                                              : std::string("n/a");

            Emit("  " + PadRight(record.m_name, kMetricNameWidth) + " "
                 + PadLeft(actual_text, 10) + " " + PadLeft(reference_text, 21) + " "
                 + PadLeft(ratio_text, 6) + "  " + record.m_grade);

            if (!record.m_note.empty())
            {
                Emit("      \xE2\x94\x94 " + record.m_note);
            }
        }

        if (performance_notes != nullptr)
        {
            Emit("  measurement notes:");
            performance_notes();
        }

        Emit("  result: " + std::to_string(session.m_metrics.size() - first_metric) + " metrics / "
             + std::to_string(performance_passed) + " checks passed / "
             + std::to_string(performance_failed) + " failed / "
             + std::to_string(performance_skipped) + " skipped / "
             + std::to_string(performance_uncovered) + " uncovered");
    }

    void Check(bool ok, std::string_view name, std::string_view detail) noexcept
    {
        Session& session = State();
        if (ok)
        {
            ++session.m_domain_passed;
        }
        else
        {
            ++session.m_domain_failed;
        }

        const char* tag = ok ? (session.m_in_performance ? "[PERF-PASS]" : "[PASS]")
                             : (session.m_in_performance ? "[PERF-FAIL]" : "[FAIL]");
        Emit("  " + std::string(tag) + " " + PadRight(name, kNameColumnWidth) + " " + std::string(detail));
    }

    void Skip(std::string_view name, std::string_view reason) noexcept
    {
        Session& session = State();
        ++session.m_domain_skipped;

        Emit("  [SKIP] " + PadRight(name, kNameColumnWidth) + " " + std::string(reason));
    }

    void Uncovered(std::string_view name, std::string_view reason) noexcept
    {
        Session& session = State();
        ++session.m_uncovered;

        // 前缀比 [PASS] 长 5 字节，名称列相应收窄，保持说明列对齐
        Emit("  [UNCOVERED] " + PadRight(name, kNameColumnWidth - 5) + " " + std::string(reason));
    }

    void Warn(std::string_view name, std::string_view detail) noexcept
    {
        Session& session = State();
        ++session.m_warnings;

        Emit("  [WARN] " + PadRight(name, kNameColumnWidth) + " " + std::string(detail));
    }

    void ReportLine(std::string_view line) noexcept
    {
        Emit(std::string(line));
    }

    void RecordMetric(const MetricDesc& metric) noexcept
    {
        Session& session = State();

        MetricRecord record;
        record.m_name = std::string(metric.name);
        record.m_basis = std::string(metric.basis);
        record.m_note = std::string(metric.note);
        record.m_actual = metric.actual_ns;
        record.m_reference = metric.reference_ns;

        record.m_grade = GradeText(metric.actual_ns, metric.reference_ns, metric.basis, metric.note,
                                   record.m_ratio, record.m_has_ratio);

        if (record.m_grade == "ungraded")
        {
            // 未评级同时计为失败口径（见 skills/benchmark 第 7 节），由退出码位 1 体现，不混入用例失败
            ++session.m_ungraded;
        }
        else
        {
            ++GradeSlot(session, record.m_grade);
        }

        session.m_metrics.push_back(std::move(record));
    }

    void EndSession() noexcept
    {
        Session& session = State();

        Emit("");
        WriteAll("--------------------------------------------------------------------");
        Emit("SUMMARY");
        // Check 计数覆盖正确性区与性能区：任一条 [FAIL]/[PERF-FAIL] 都置退出码位 0
        Emit("  checks      : " + std::to_string(session.m_domain_count) + " domains / "
             + std::to_string(session.m_passed) + " passed / "
             + std::to_string(session.m_case_failed) + " failed / "
             + std::to_string(session.m_skipped) + " skipped"
             + "（correctness + performance 两个区的 Check 合计）");
        Emit("  uncovered   : " + std::to_string(session.m_uncovered)
             + "（应覆盖但有客观障碍的项，见 [UNCOVERED] 行；不计为失败）");
        Emit("  warnings    : " + std::to_string(session.m_warnings)
             + "（环境降级项，不影响正确性判定与退出码）");
        Emit("  performance : " + std::to_string(session.m_domain_count) + " domains / "
             + std::to_string(session.m_metrics.size()) + " metrics");

        Emit("                world-class=" + std::to_string(session.m_grade_counts[0])
             + " excellent=" + std::to_string(session.m_grade_counts[1])
             + " good=" + std::to_string(session.m_grade_counts[2])
             + " fair=" + std::to_string(session.m_grade_counts[3])
             + " poor=" + std::to_string(session.m_grade_counts[4])
             + " terrible=" + std::to_string(session.m_grade_counts[5])
             + " ungraded=" + std::to_string(session.m_ungraded));

        const int code = ExitCode();
        std::string code_text = "  exit code   : " + std::to_string(code);
        if (code == 0)
        {
            code_text += " (all passed, all metrics graded)";
        }
        else
        {
            code_text += " (";
            bool first = true;
            if ((code & 1) != 0)
            {
                code_text += "case-failures";
                first = false;
            }
            if ((code & 2) != 0)
            {
                if (!first)
                {
                    code_text += ", ";
                }
                code_text += "ungraded-metrics";
                first = false;
            }
            if ((code & 4) != 0)
            {
                if (!first)
                {
                    code_text += ", ";
                }
                code_text += "report-write-failure";
            }
            code_text += ")";
        }
        Emit(code_text);

        Emit("  report write: " + std::string(session.m_report_failed ? "FAILED" : "ok"));

        if (session.m_report.is_open())
        {
            session.m_report.flush();
            session.m_report.close();
        }
        session.m_report_open = false;
    }

    int ExitCode() noexcept
    {
        Session& session = State();

        int code = 0;
        if (session.m_case_failed > 0)
        {
            code |= 1;
        }
        if (session.m_ungraded > 0)
        {
            code |= 2;
        }
        if (session.m_report_failed || (session.m_begun && !session.m_report_ok))
        {
            code |= 4;
        }
        return code;
    }
} // namespace ID::Bench
