#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "Core/Core.hpp"

/**
 *  Benchmark 公共设施：会话、双通道输出、计时、用例记录与评级。
 *  
 *  规则（与 skills/benchmark 的分册一致）：
 *  - 控制台与官方报告走同一条输出通道：`Emit` 同时写 stdout 与报告文件，
 *    用例内不得直接 printf/std::cout，否则两者会不一致。
 *  - `Check` / `Skip` / `Uncovered` / `Warn` / `RecordMetric` 不抛异常、不终止进程：只记录并计数。
 *  - 退出码：位 0 = 有 `Check` 失败（正确性区与性能区的 Check 一律计入），位 1 = 有未评级（ungraded）指标，
 *    位 2 = 报告写失败。`Skip` / `Uncovered` / `Warn` 不改变退出码。
 *  - `Skip` 只表达"该构建组合未选中被测对象"或"契约明确非目标/平台不适用"，必须写明对象与原因；
 *    其它确实没取证的项目用 `Uncovered` 登记（报告 `[UNCOVERED]` 行 + SUMMARY 计数），
 *    不得把"没跑/无法注入"包装成 `Skip`。
 *  - 参考值没有可核实依据时 `basis` 传空：依据列显示 "-"，该指标 `ungraded` 并计入失败；
 *    合法 `basis` 只有 theoretical / hardware / os-baseline / regression / budget，其它取值按非法依据处理。
 *  - `note` 为空时同样判为 `ungraded`（依据无法复核）。
 *  - `Timer` 与 `NowNs` 共享同一计时源（Windows 为 QueryPerformanceCounter），
 *    分辨率在 `BeginSession` 探测并打印在报告头。
 *  - `StdoutRedirect` 在窗口内改动进程 stdout 的文件描述符：窗口内不得产生本设施的输出
 *    （`Check`/`Skip`/`RecordMetric`/域标题），否则控制台与报告会不一致。
 */

// 构建身份与路径由 CMake 注入；缺失时如实显示 unknown，不猜值
#if !defined(BENCHMARK_REPORT_PATH)
    #define BENCHMARK_REPORT_PATH "report.txt"
#endif

#if !defined(BENCHMARK_TEMP_DIR)
    #define BENCHMARK_TEMP_DIR "tmp"
#endif

#if !defined(BENCHMARK_BUILD_CONFIG)
    #define BENCHMARK_BUILD_CONFIG "unknown"
#endif

#if !defined(BENCHMARK_LINK_MODE)
    #define BENCHMARK_LINK_MODE "unknown"
#endif

#if !defined(BENCHMARK_LTO)
    #define BENCHMARK_LTO "unknown"
#endif

#if !defined(BENCHMARK_ARCH)
    #define BENCHMARK_ARCH "unknown"
#endif

namespace ID::Bench
{
    using TestFn = void (*)() noexcept;

    /**
     *  @brief 打开会话：创建临时目录、打开官方报告、打印环境头并探测计时分辨率
     *  @note 报告打开失败只关闭报告通道，控制台仍有效，退出码体现报告失败
     */
    void BeginSession() noexcept;

    /**
     *  @brief 运行一个测试域：先正确性、后性能，各自输出标题与 result 行
     *  @param domain 域名（报告标题的一部分）
     *  @param correctness 正确性入口，可为 nullptr
     *  @param performance 性能入口，可为 nullptr
     *  @param performance_notes 性能说明入口，可为 nullptr；在指标表之后输出统计口径与覆盖局限
     *  @note 两个区的 `Check` 都计入全局失败计数（性能区的行用 `[PERF-PASS]` / `[PERF-FAIL]` 前缀，
     *        不与指标表混排）
     */
    void Run(std::string_view domain, TestFn correctness, TestFn performance,
             TestFn performance_notes = nullptr) noexcept;

    /// @brief 打印 SUMMARY、关闭报告
    void EndSession() noexcept;

    /// @brief 进程退出码（见文件头的位定义）
    int ExitCode() noexcept;

    /**
     *  @brief 记录一条用例判定
     *  @param ok 判定结果
     *  @param name 用例名
     *  @param detail 可核对的说明（期望与实际的契约依据）
     *  @note 性能区内调用时输出 `[PERF-PASS]` / `[PERF-FAIL]` 前缀，但仍计入失败与退出码
     */
    void Check(bool ok, std::string_view name, std::string_view detail = {}) noexcept;

    /**
     *  @brief 记录一条 Skip
     *  @note 只允许表达"该组合未选中被测对象"或"契约明确非目标/平台不适用"，必须写明原因
     */
    void Skip(std::string_view name, std::string_view reason) noexcept;

    /**
     *  @brief 登记一条"本次未覆盖"的项（`[UNCOVERED]` 行，计入 SUMMARY，不计为失败）
     *  @param name 项目名
     *  @param reason 为什么无法覆盖（例如"不改被测实现无法受控注入"）
     *  @note 与 `Skip` 分开：Skip 只表达未选中/契约非目标，这里的项是"应该覆盖但有客观障碍"
     */
    void Uncovered(std::string_view name, std::string_view reason) noexcept;

    /**
     *  @brief 记录一条告警（`[WARN]` 行，计入 SUMMARY，不计为失败）
     *  @param name 告警项名
     *  @param detail 事实与影响（例如"绑核失败 → 样本可能含线程迁移噪声"）
     *  @note 用于"环境降级但仍可继续"的情形：不影响正确性判定，只如实标注
     */
    void Warn(std::string_view name, std::string_view detail) noexcept;

    /**
     *  @brief 输出一行自由格式报告文本（统计口径、覆盖局限等说明用，不参与计数）
     *  @param line 整行内容（含调用方需要的缩进前缀）
     *  @note 仍走同一输出通道：用例不得自己写 stdout，否则控制台与报告会不一致
     */
    void ReportLine(std::string_view line) noexcept;

    /// @brief 一条性能指标的五个强制字段
    struct MetricDesc
    {
        std::string_view    name         = {};
        double              actual_ns    = 0.0;
        double              reference_ns = 0.0;
        std::string_view    basis        = {};      // 无依据时留空：报告依据列显示 "-"
        std::string_view    note         = {};
    };

    /// @brief 记录一条指标；缺依据、缺 note 或数值非法时输出 ungraded 并计入失败
    void RecordMetric(const MetricDesc& metric) noexcept;

    /**
     *  @brief 一段经过时间的计时器（构造即开始）
     */
    class Timer
    {
    public:
        Timer() noexcept;
        double ElapsedNs() const noexcept;

    private:
        std::int64_t    m_start_ticks = 0;
    };

    /// @brief 单调纳秒时间戳（与 Timer 同源；用于逐条延迟采样，采样区间内不分配）
    double NowNs() noexcept;

    /// @brief 计时源分辨率（纳秒）：取探测到的最小可分辨步长与 1e9/频率 的较大者
    double TimerResolutionNs() noexcept;

    /// @brief 计时源名称（写进报告头）
    const char* TimerSourceName() noexcept;

    /// @brief 计时源频率（Hz；非 Windows 平台为 0）
    double TimerFrequencyHz() noexcept;

    /// @brief 多轮样本统计：中位数、极值、极差比例与不稳定标记
    struct SampleStats
    {
        double  median       = 0.0;
        double  minimum      = 0.0;     // 命名避开 min/max 宏（未定义 NOMINMAX 的 Windows 头会定义它们）
        double  maximum      = 0.0;
        double  spread       = 0.0;     // (maximum - minimum) / median
        bool    is_unstable  = false;   // 极差超过 20%
    };

    /// @brief 就地排序后求中位数与极值；样本数为 0 时全部返回 0
    SampleStats Summarize(double* samples, std::size_t count) noexcept;

    /// @brief 从已排序样本取分位数（fraction 取 [0,1]）
    double PercentileOfSorted(const double* sorted_samples, std::size_t count, double fraction) noexcept;

    /**
     *  @brief 固定小数位的数字文本
     *  @note 报告拼接统一走本函数 + std::string 连接：本机 MinGW-w64 + UCRT（GCC 16.2.0）下，
     *        一次传入 11 个可变参数的 snprintf 在写报告时触发访问冲突（0xC0000005，独立探针
     *        `fixtures/toolchain-probe/format_probe.cpp` 可复现，证据见该变体目录下的
     *        `toolchain-probe/format-probe.log`），因此每次格式化只传一个 double，其余用字符串连接
     */
    std::string FormatDouble(double value, int decimals) noexcept;

    /// @brief 右侧补空格到至少 width 字节（ASCII 名称列使用）
    std::string PadRight(std::string_view text, std::size_t width) noexcept;

    /// @brief 左侧补空格到至少 width 字节（数值列使用）
    std::string PadLeft(std::string_view text, std::size_t width) noexcept;

    /// @brief 防优化：把指针写入跨翻译单元可见的 sink（调用点在计时区间之外，每批一次）
    void KeepAliveSink(const void* pointer) noexcept;

    ID_FORCEINLINE void KeepAlive(const void* pointer) noexcept
    {
        KeepAliveSink(pointer);
    }

    /**
     *  @brief 临时文件绝对路径（临时目录由构建注入，与工作目录无关）
     *  @param name 文件名部分
     */
    std::string TempPath(std::string_view name);

    /// @brief 当前线程绑核；core < 0 表示解绑
    bool SetAffinity(int core) noexcept;

    /// @brief 逻辑核数（用于选择绑核与报告）
    int LogicalCoreCount() noexcept;

    /**
     *  @brief 把进程 stdout 的文件描述符临时改指向目标（文件路径或 "NUL"）
     *  @note 窗口内不得产生本设施的输出；`End` 之后 stdout 恢复原目标
     *  @note 用途：让 console sink 的写出离开真实终端，既不淹没有效报告，也不把终端渲染成本算进测量
     *  @note 非 Windows 平台尚未实现（未声称 Linux 支持）：`Begin` 恒返回 false，
     *        依赖它的用例按"环境不支持"记 Skip，不会污染真实控制台
     */
    class StdoutRedirect
    {
    public:
        StdoutRedirect() noexcept = default;
        ~StdoutRedirect() noexcept;

        StdoutRedirect(const StdoutRedirect&) = delete;
        StdoutRedirect& operator=(const StdoutRedirect&) = delete;

        /// @brief 开始重定向；目标无法打开时返回 false 且不改变任何状态
        bool Begin(const char* target_path) noexcept;

        /// @brief 冲刷并恢复 stdout
        void End() noexcept;

    private:
        int m_saved_fd = -1;
    };
} // namespace ID::Bench
