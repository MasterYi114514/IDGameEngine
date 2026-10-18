#include "benchmark_support.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>
#else
    #include <chrono>
#endif

#if !defined(_MSC_VER) && defined(_WIN32)
    #include <cpuid.h>
#endif

/**
 *  Benchmark 公共设施实现
 *  1. 输出：单条 1KB 栈上缓冲格式化后交给 tee，同时写 stdout 与 report.txt，保证两边字节一致；
 *  2. 计时：Windows 走 QPC，其它平台走 steady_clock；分辨率在 BeginSession 时探测并打印；
 *  3. 评级：ratio 分档表与 skills/benchmark/references/grading.md 第 5 节逐字对应。
 */

#ifndef ID_BENCHMARK_REPORT_DIR
    #define ID_BENCHMARK_REPORT_DIR "."
#endif

// CMake 未注入构建开关时按全 OFF 处理，保证单文件也能编译
#ifndef ID_BENCH_CONFIG
    #define ID_BENCH_CONFIG "unknown"
#endif
#ifndef ID_BENCH_LTO
    #define ID_BENCH_LTO 0
#endif
#ifndef ID_BENCH_AVX2
    #define ID_BENCH_AVX2 0
#endif
#ifndef ID_BENCH_MONOLITHIC
    #define ID_BENCH_MONOLITHIC 0
#endif

#define ID_BENCH_STRINGIFY_IMPL(x) #x
#define ID_BENCH_STRINGIFY(x)      ID_BENCH_STRINGIFY_IMPL(x)

namespace
{
    // report.txt 的绝对路径由 CMake 以编译期宏注入，与运行时的 CWD 无关
    constexpr const char* kReportPath = ID_BENCHMARK_REPORT_DIR "/report.txt";

    // 单条输出行的栈上缓冲长度；超出部分截断，宁可截断也不能截断到不同步
    constexpr ID::size_t kLineCapacity = 1024;

    // 六档评级 + ungraded 的计数下标
    enum GradeIndex : ID::size_t
    {
        kGradeWorldClass = 0,
        kGradeExcellent,
        kGradeGood,
        kGradeFair,
        kGradePoor,
        kGradeTerrible,
        kGradeUngraded,
        kGradeCount
    };

    FILE*       g_report_file      = nullptr;
    bool        g_session_begun    = false;
    int         g_extra_exit_code  = 0;
    double      g_timer_ticks_per_s = 0.0;
    double      g_timer_resolution_ns = 0.0;
    const char* g_affinity_text    = "unpinned";

    // 全局累计
    ID::size_t  g_domain_count      = 0;
    ID::size_t  g_total_pass        = 0;
    ID::size_t  g_total_fail        = 0;
    ID::size_t  g_total_skip        = 0;
    ID::size_t  g_metric_count      = 0;
    ID::size_t  g_grade_count[kGradeCount] = { 0, 0, 0, 0, 0, 0, 0 };

    // 当前域的计数（每个 Run 开始时清零）
    ID::size_t  g_domain_pass       = 0;
    ID::size_t  g_domain_fail       = 0;
    ID::size_t  g_domain_skip       = 0;

    // KeepAlive 的落地位置：跨 TU 的 volatile 写，编译器无法证明它没有副作用
    volatile const void* g_keepalive_sink = nullptr;

    /**
     *  @brief 把一段字节同时写到 console 与 report.txt
     *  @note stdout 与文件写入顺序一致，任一侧失败都不影响另一侧
     */
    void tee(const char* text, ID::size_t length) noexcept
    {
        if (length == 0)
        {
            return;
        }
        std::fwrite(text, 1, length, stdout);
        if (g_report_file != nullptr)
        {
            std::fwrite(text, 1, length, g_report_file);
        }
    }

    // 格式化一行并 tee 出去；格式化失败（编码错误）时什么都不写
    void emit(const char* format, ...) noexcept
    {
        char    line[kLineCapacity];
        va_list args;
        va_start(args, format);
        const int length = std::vsnprintf(line, sizeof(line), format, args);
        va_end(args);

        if (length <= 0)
        {
            return;
        }
        const ID::size_t write_length = static_cast<ID::size_t>(length) < sizeof(line)
                                      ? static_cast<ID::size_t>(length)
                                      : sizeof(line) - 1;
        tee(line, write_length);
    }

    // 用例行：  [PASS] <name 左对齐 32 列> <detail>
    void emit_case(const char* tag, std::string_view name, std::string_view detail) noexcept
    {
        if (detail.empty())
        {
            emit("  [%s] %-32.*s\n", tag, static_cast<int>(name.size()), name.data());
        }
        else
        {
            emit("  [%s] %-32.*s %.*s\n", tag,
                 static_cast<int>(name.size()), name.data(),
                 static_cast<int>(detail.size()), detail.data());
        }
    }

    // 指标表头与表体共用同一组列宽：name=32 / actual=9 / reference=9 / basis=13 / ratio=5 / grade
    void emit_metric_header() noexcept
    {
        emit("  %-32s %9s %9s %-13s %5s  %s\n", "metric", "actual", "reference", "", "ratio", "grade");
        emit("  ---------------------------------------------------------------------------------\n");
    }

    // 合法的 basis 只有四种；传别的值必须被判为 invalid-basis，防止绕过"参考值要有依据"
    bool is_valid_basis(const char* basis) noexcept
    {
        if (basis == nullptr)
        {
            return false;
        }
        return std::strcmp(basis, "os-baseline") == 0
            || std::strcmp(basis, "theoretical") == 0
            || std::strcmp(basis, "hardware")    == 0
            || std::strcmp(basis, "regression")  == 0;
    }

    /**
     *  @brief 探测 CPU 品牌串（扩展 CPUID 0x80000002..0x80000004）
     *  @param out 输出缓冲，取不到时写 "unknown"
     *  @param capacity 缓冲字节数
     */
    void detect_cpu_brand(char* out, ID::size_t capacity) noexcept
    {
        out[0] = '\0';

        #if defined(_MSC_VER)
            int registers[4] = { 0, 0, 0, 0 };
            __cpuidex(registers, 0x80000000, 0);
            const unsigned max_extended = static_cast<unsigned>(registers[0]);
            if (max_extended >= 0x80000004u)
            {
                char brand[49] = {};
                for (unsigned leaf = 0; leaf < 3; ++leaf)
                {
                    __cpuidex(registers, static_cast<int>(0x80000002u + leaf), 0);
                    std::memcpy(brand + leaf * 16, registers, 16);
                }
                std::snprintf(out, capacity, "%s", brand);
            }
        #elif defined(_WIN32)
            unsigned eax = 0;
            unsigned ebx = 0;
            unsigned ecx = 0;
            unsigned edx = 0;
            if (__get_cpuid(0x80000000u, &eax, &ebx, &ecx, &edx) != 0 && eax >= 0x80000004u)
            {
                char brand[49] = {};
                for (unsigned leaf = 0; leaf < 3; ++leaf)
                {
                    if (__get_cpuid(0x80000002u + leaf, &eax, &ebx, &ecx, &edx) == 0)
                    {
                        brand[0] = '\0';
                        break;
                    }
                    std::memcpy(brand + leaf * 16,      &eax, 4);
                    std::memcpy(brand + leaf * 16 + 4,  &ebx, 4);
                    std::memcpy(brand + leaf * 16 + 8,  &ecx, 4);
                    std::memcpy(brand + leaf * 16 + 12, &edx, 4);
                }
                std::snprintf(out, capacity, "%s", brand);
            }
        #endif

        // CPUID 的品牌串前面常带一串空格（Intel 用空格填充），去掉首尾空白
        ID::size_t begin = 0;
        while (out[begin] == ' ')
        {
            ++begin;
        }
        if (begin != 0)
        {
            std::memmove(out, out + begin, std::strlen(out + begin) + 1);
        }
        ID::size_t end = std::strlen(out);
        while (end > 0 && out[end - 1] == ' ')
        {
            out[--end] = '\0';
        }

        if (out[0] == '\0')
        {
            std::snprintf(out, capacity, "unknown");
        }
    }

    // 编译期/构建期信息：由 CMake 以宏注入，缺失时按 Debug + 全 OFF 处理
    const char* build_configuration() noexcept
    {
        #if defined(ID_BENCH_CONFIG)
            return ID_BENCH_CONFIG;
        #else
            return "unknown";
        #endif
    }

    const char* build_switch(bool enabled) noexcept
    {
        return enabled ? "ON" : "OFF";
    }

    void detect_timer() noexcept
    {
        #if defined(_WIN32)
            LARGE_INTEGER frequency;
            QueryPerformanceFrequency(&frequency);
            g_timer_ticks_per_s = static_cast<double>(frequency.QuadPart);
        #else
            g_timer_ticks_per_s = 1.0e9;   // steady_clock 直接按 ns 计数
        #endif

        if (g_timer_ticks_per_s <= 0.0)
        {
            g_timer_ticks_per_s = 1.0;
        }
        g_timer_resolution_ns = 1.0e9 / g_timer_ticks_per_s;
    }

    /**
     *  @brief 把当前线程绑到第一个逻辑核
     *  @note 混合架构（P-core + E-core）上调度器会把线程在快慢核之间迁移，同一段循环能差出 2 倍，
     *        微基准必须先固定核心；绑定结果写进报告头，避免读数失去前提
     */
    void pin_current_thread() noexcept
    {
        #if defined(_WIN32)
            if (SetThreadAffinityMask(GetCurrentThread(), static_cast<DWORD_PTR>(1)) != 0)
            {
                g_affinity_text = "core 0";
            }
        #endif
    }
} // namespace

namespace ID::Bench
{
    void BeginSession() noexcept
    {
        if (g_session_begun)
        {
            return;
        }
        g_session_begun = true;

        #if defined(_WIN32)
            // report.txt 与 console 写的是同一串 UTF-8 字节，控制台代码页必须一起改成 UTF-8
            SetConsoleOutputCP(CP_UTF8);
        #endif

        detect_timer();
        pin_current_thread();

        g_report_file = std::fopen(kReportPath, "w");
        if (g_report_file == nullptr)
        {
            g_extra_exit_code |= 2;
        }

        char timestamp[32] = "unknown";
        #if defined(_WIN32)
            SYSTEMTIME local_time;
            GetLocalTime(&local_time);
            std::snprintf(timestamp, sizeof(timestamp), "%04u-%02u-%02u %02u:%02u:%02u",
                          static_cast<unsigned>(local_time.wYear),  static_cast<unsigned>(local_time.wMonth),
                          static_cast<unsigned>(local_time.wDay),   static_cast<unsigned>(local_time.wHour),
                          static_cast<unsigned>(local_time.wMinute), static_cast<unsigned>(local_time.wSecond));
        #else
            const std::time_t now = std::time(nullptr);
            std::tm local_time{};
            if (now != static_cast<std::time_t>(-1) && localtime_r(&now, &local_time) != nullptr)
            {
                if (std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &local_time) == 0)
                {
                    std::snprintf(timestamp, sizeof(timestamp), "unknown");
                }
            }
        #endif

        char cpu_brand[64] = {};
        detect_cpu_brand(cpu_brand, sizeof(cpu_brand));

        #if defined(_MSC_VER)
            const char* compiler = "MSVC " ID_BENCH_STRINGIFY(_MSC_VER);
        #elif defined(__clang__)
            const char* compiler = "Clang " __clang_version__;
        #elif defined(__GNUC__)
            const char* compiler = "GCC " __VERSION__;
        #else
            const char* compiler = "unknown";
        #endif

        emit("=== IDGameEngine Benchmark Report ===\n");
        emit("generated     : %s\n", timestamp);
        emit("compiler      : %s\n", compiler);
        emit("build         : %s | LTO=%s | AVX2=%s | MONOLITHIC=%s\n",
             build_configuration(),
             build_switch(ID_BENCH_LTO != 0),
             build_switch(ID_BENCH_AVX2 != 0),
             build_switch(ID_BENCH_MONOLITHIC != 0));
        emit("timer         : %s (freq=%.0f, resolution=%.0f ns) | affinity=%s\n",
             #if defined(_WIN32)
                 "QPC",
             #else
                 "steady_clock",
             #endif
             g_timer_ticks_per_s, g_timer_resolution_ns, g_affinity_text);
        emit("cpu           : %s\n", cpu_brand);
        emit("report        : %s\n", kReportPath);
        emit("--------------------------------------------------------------------\n");

        if (g_report_file == nullptr)
        {
            emit("[WARN] cannot open report file, console output is still valid\n");
        }
        if (g_timer_resolution_ns > 100.0)
        {
            // 分辨率不够时，小于 1 微秒的指标会被量化台阶污染
            emit("[WARN] timer resolution %.0f ns > 100 ns, small metrics are quantized\n", g_timer_resolution_ns);
        }
    }

    void Run(std::string_view domain, TestFn security, TestFn performance) noexcept
    {
        ++g_domain_count;

        if (security != nullptr)
        {
            g_domain_pass = 0;
            g_domain_fail = 0;
            g_domain_skip = 0;

            emit("\n## %.*s — Security\n", static_cast<int>(domain.size()), domain.data());
            security();

            emit("  result: %zu passed / %zu failed / %zu skipped\n",
                 g_domain_pass, g_domain_fail, g_domain_skip);

            g_total_pass += g_domain_pass;
            g_total_fail += g_domain_fail;
            g_total_skip += g_domain_skip;
        }

        if (performance != nullptr)
        {
            // 性能段的 ungraded 也计入 FAIL，但这里单独结算，不污染下一个域的 Security 结果行
            g_domain_pass = 0;
            g_domain_fail = 0;
            g_domain_skip = 0;

            emit("\n## %.*s — Performance\n", static_cast<int>(domain.size()), domain.data());
            emit_metric_header();
            performance();

            g_total_fail += g_domain_fail;
            g_domain_pass = 0;
            g_domain_fail = 0;
            g_domain_skip = 0;
        }
    }

    void EndSession() noexcept
    {
        if (!g_session_begun)
        {
            return;
        }

        const int exit_code = ExitCode();

        emit("\n--------------------------------------------------------------------\n");
        emit("SUMMARY\n");
        emit("  security    : %zu domains / %zu passed / %zu failed / %zu skipped\n",
             g_domain_count, g_total_pass, g_total_fail, g_total_skip);
        emit("  performance : %zu domains / %zu metrics\n", g_domain_count, g_metric_count);
        emit("                world-class=%zu  excellent=%zu  good=%zu  fair=%zu  poor=%zu  terrible=%zu  ungraded=%zu\n",
             g_grade_count[kGradeWorldClass], g_grade_count[kGradeExcellent], g_grade_count[kGradeGood],
             g_grade_count[kGradeFair], g_grade_count[kGradePoor], g_grade_count[kGradeTerrible],
             g_grade_count[kGradeUngraded]);
        emit("  exit code   : %d\n", exit_code);

        if (g_report_file != nullptr)
        {
            std::fflush(g_report_file);
            std::fclose(g_report_file);
            g_report_file = nullptr;
        }
        std::fflush(stdout);
        g_session_begun = false;
    }

    int ExitCode() noexcept
    {
        int code = g_extra_exit_code;
        if (g_total_fail > 0)
        {
            code |= 1;
        }
        return code;
    }

    void Check(bool ok, std::string_view name, std::string_view detail) noexcept
    {
        if (ok)
        {
            ++g_domain_pass;
        }
        else
        {
            ++g_domain_fail;
        }
        emit_case(ok ? "PASS" : "FAIL", name, detail);
    }

    void Skip(std::string_view name, std::string_view reason) noexcept
    {
        ++g_domain_skip;
        emit_case("SKIP", name, reason);
    }

    void RecordMetric(const MetricDesc& metric) noexcept
    {
        const bool basis_ok    = is_valid_basis(metric.basis);
        const bool graded      = basis_ok
                              && !metric.name.empty()
                              && !metric.note.empty()
                              && metric.actual_ns > 0.0
                              && metric.reference_ns > 0.0;
        const double ratio     = graded ? (metric.actual_ns / metric.reference_ns) : 0.0;
        const std::string_view grade = graded ? GradeOf(ratio) : std::string_view{ "ungraded" };

        char actual_text[32]    = "n/a";
        char reference_text[32] = "n/a";
        char basis_text[32]     = "[invalid-basis]";
        char ratio_text[16]     = "n/a";

        if (basis_ok)
        {
            std::snprintf(basis_text, sizeof(basis_text), "[%s]", metric.basis);
        }
        if (metric.actual_ns > 0.0)
        {
            std::snprintf(actual_text, sizeof(actual_text), "%.2f ns", metric.actual_ns);
        }
        if (basis_ok && metric.reference_ns > 0.0)
        {
            std::snprintf(reference_text, sizeof(reference_text), "%.2f ns", metric.reference_ns);
        }
        if (graded)
        {
            std::snprintf(ratio_text, sizeof(ratio_text), "%.2f", ratio);
        }

        ++g_metric_count;
        if (graded)
        {
            ID::size_t index = kGradeTerrible;
            if (ratio <= 0.50)
            {
                index = kGradeWorldClass;
            }
            else if (ratio <= 0.80)
            {
                index = kGradeExcellent;
            }
            else if (ratio <= 1.20)
            {
                index = kGradeGood;
            }
            else if (ratio <= 2.00)
            {
                index = kGradeFair;
            }
            else if (ratio <= 4.00)
            {
                index = kGradePoor;
            }
            ++g_grade_count[index];
        }
        else
        {
            // 缺依据的指标不允许"静默通过"：计 ungraded 的同时计入 FAIL
            ++g_grade_count[kGradeUngraded];
            ++g_domain_fail;
        }

        emit("  %-32.*s %9s %9s %-13s %5s  %.*s\n",
             static_cast<int>(metric.name.size()), metric.name.data(),
             actual_text, reference_text, basis_text, ratio_text,
             static_cast<int>(grade.size()), grade.data());

        if (!metric.note.empty())
        {
            emit("      └ %.*s\n", static_cast<int>(metric.note.size()), metric.note.data());
        }
    }

    Timer::Timer() noexcept
    {
        if (g_timer_ticks_per_s <= 0.0)
        {
            detect_timer();
        }

        #if defined(_WIN32)
            LARGE_INTEGER counter;
            QueryPerformanceCounter(&counter);
            m_begin = static_cast<ID::int64_t>(counter.QuadPart);
        #else
            const auto ticks = std::chrono::steady_clock::now().time_since_epoch();
            m_begin = static_cast<ID::int64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(ticks).count());
        #endif
    }

    double Timer::ElapsedNs() const noexcept
    {
        #if defined(_WIN32)
            LARGE_INTEGER counter;
            QueryPerformanceCounter(&counter);
            const ID::int64_t elapsed = static_cast<ID::int64_t>(counter.QuadPart) - m_begin;
            return static_cast<double>(elapsed) * 1.0e9 / g_timer_ticks_per_s;
        #else
            const auto ticks = std::chrono::steady_clock::now().time_since_epoch();
            const ID::int64_t now = static_cast<ID::int64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(ticks).count());
            return static_cast<double>(now - m_begin);
        #endif
    }

    void KeepAliveSink(const void* p) noexcept
    {
        g_keepalive_sink = p;
    }

    std::string_view GradeOf(double ratio) noexcept
    {
        // 与 skills/benchmark/references/grading.md 第 5 节逐字一致的六档
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
} // namespace ID::Bench
