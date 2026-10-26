#pragma once

namespace ID::Bench
{
    void IdLogCorrectnessTest() noexcept;
    void IdLogPerformanceTest() noexcept;

    /// @brief 性能区的统计口径与覆盖局限说明（在指标表之后输出，不混入指标行）
    void IdLogPerformanceNotes() noexcept;

    /**
     *  @brief fatal 语义的子进程入口（子进程隔离用例，不作为独立测试域）
     *  @param log_path 子进程使用的日志文件路径
     *  @return 仅在"未按契约终止"时返回非 0；正常路径由 std::abort() 终止进程
     *  @note 契约要求 fatal 终止进程，同进程无法取证，因此由主入口按参数分裂出独立进程
     */
    int RunFatalChildCase(const char* log_path) noexcept;

    /**
     *  @brief 陈旧 `Logger*` 违约用法的子进程入口（子进程隔离用例，不作为独立测试域）
     *  @param log_path 子进程使用的日志文件路径
     *  @return 正常收尾返回 0（实现行为：不崩溃、消息被丢弃；违约用法，契约不承诺安全）；前置失败返回非 0
     *  @note 违约用法可能崩溃，同一进程内取证会带走整个测试进程，因此由主入口分裂出独立进程
     */
    int RunStalePointerChildCase(const char* log_path) noexcept;

    /**
     *  @brief 已销毁日志器的配置调用子进程入口（子进程隔离用例，不作为独立测试域）
     *  @return 四个配置方法都返回 false 且正常收尾返回 0；前置失败或某方法未返回 false 返回 0xC1–0xC6
     *  @note 通过陈旧 `Logger*` 调用配置方法属于违约用法，实现行为是"不崩溃 + 返回 false"（契约不承诺安全），
     *        同进程取证若崩溃会带走整个测试进程，因此由主入口分裂出独立进程
     */
    int RunDestroyedConfigChildCase() noexcept;
} // namespace ID::Bench
