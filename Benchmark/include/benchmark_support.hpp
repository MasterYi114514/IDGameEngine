#pragma once

#include <cstddef>
#include <string_view>

#include "Common/Core.hpp"
#include "Kernel/CoreTypes.hpp"

/**
 *  Benchmark 公共设施：会话、输出 tee、计时、用例记录与评级
 *  1. 全部 console 与 report.txt 输出都经过内部同一个 tee，保证两边逐字一致；
 *     本设施之外的任何地方禁止直接写 stdout/stderr（否则报告与屏幕会分叉）。
 *  2. Check / Skip / RecordMetric 都不抛异常、不 abort：正确性用例失败只记录并累加计数，
 *     保证第一个失败不会杀掉后面的用例。
 *  3. 性能指标评级口径见 skills/benchmark/references/grading.md 第 5 节，本设施的评级表必须与之逐字一致。
 */

namespace ID::Bench
{
    // 测试函数签名：正确性与性能两个入口形态一致，都不允许抛异常
    using TestFn = void (*)() noexcept;

    // ---- 会话 ----

    /**
     *  @brief 打印 8 行环境头，并以 "w" 模式打开 report.txt（每次运行覆盖，永远只有一个文件）
     *  @note 重复调用无效；report.txt 打不开时打印警告并把退出码置上 2 位，console 输出仍然有效
     */
    void BeginSession() noexcept;

    /**
     *  @brief 跑一个测试域：先 Security 再 Performance，各自打印域标题与结果行
     *  @param domain 域名，与插件 ID 同名，例如 "ID.Kernel"
     *  @param security 正确性入口，可为 nullptr（表示该域不覆盖正确性）
     *  @param performance 性能入口，可为 nullptr（表示该域不覆盖性能）
     */
    void Run(std::string_view domain, TestFn security, TestFn performance) noexcept;

    /**
     *  @brief 打印 SUMMARY（含各档等级计数与 exit code），flush 并关闭 report.txt
     */
    void EndSession() noexcept;

    /**
     *  @brief 进程退出码
     *  @return 全部用例通过返回 0；有 FAIL 返回 1；report.txt 打不开额外置 2 位
     */
    int ExitCode() noexcept;

    // ---- 正确性 ----

    /**
     *  @brief 记录一条正确性用例结果
     *  @param ok 用例是否通过
     *  @param name 用例名，建议形如 "slot.register_and_get"（左对齐 32 列输出）
     *  @param detail 一句话说明期望行为；为空时不输出该列
     *  @note 失败只累加计数，不 abort、不抛异常
     */
    void Check(bool ok, std::string_view name, std::string_view detail = {}) noexcept;

    /**
     *  @brief 记录一条被跳过的用例
     *  @param name 用例名
     *  @param reason 跳过原因；契约本身就是 abort 的路径必须写清"需子进程隔离"
     */
    void Skip(std::string_view name, std::string_view reason) noexcept;

    // ---- 性能 ----

    // 一条性能指标的完整描述。五个字段缺一不可，缺少依据时整条按 ungraded 输出并计入 FAIL
    struct MetricDesc
    {
        std::string_view    name            = {};       // 指标名，例如 "memory.alloc_free_16B"
        double              actual_ns       = 0.0;      // 本次运行实测 ns/op
        double              reference_ns    = 0.0;      // 参考值，必须来自合法依据
        const char*         basis           = "";       // "os-baseline" | "theoretical" | "hardware" | "regression"
        std::string_view    note            = {};       // 一句话：参考值怎么来的
    };

    /**
     *  @brief 输出一行指标（name / actual / reference / basis / ratio / grade）并累加统计
     *  @param metric 指标描述；basis 非法、reference_ns <= 0、actual_ns <= 0、note 为空时按 ungraded + FAIL 处理
     */
    void RecordMetric(const MetricDesc& metric) noexcept;

    // ---- 计时 ----

    /**
     *  @brief 单调高分辨率计时器（Windows 用 QPC，其它平台用 steady_clock）
     *  @note 构造函数取起点，ElapsedNs() 返回整段经过时间；换算成 ns/op 由用例自己除以迭代数
     */
    class Timer
    {
    public:
        Timer() noexcept;
        double ElapsedNs() const noexcept;

    private:
        ID::int64_t m_begin = 0;
    };

    // ---- 防死代码消除 ----

    /**
     *  @brief 把 p 写入支持库内部的 volatile sink，让编译器无法删除产生 p 的计算
     *  @param p 需要在计时结束后仍"被使用"的指针或数据地址
     *  @note 每次调用是一次跨 TU 的 volatile 写，成本约 1-2 cycles；应在计时区间之外调用，
     *        不要放进逐次迭代的循环体里
     */
    void KeepAliveSink(const void* p) noexcept;

    // KeepAlive 的薄封装，保证调用点零额外开销
    ID_FORCEINLINE void KeepAlive(const void* p) noexcept
    {
        KeepAliveSink(p);
    }

    // ---- 评级 ----

    /**
     *  @brief 按 ratio = actual / reference 取评级
     *  @param ratio 实测值与参考值之比
     *  @return world-class / excellent / good / fair / poor / terrible
     */
    std::string_view GradeOf(double ratio) noexcept;
} // namespace ID::Bench
