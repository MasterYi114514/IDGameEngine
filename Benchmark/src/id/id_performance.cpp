#include "benchmark_id.hpp"

#include "benchmark_support.hpp"

/**
 *  ID 域性能入口：转发 id.cmake 子域的工具耗时用例。
 *
 *  为什么只有一项指标：工具耗时不是游戏性能，且没有可核实依据（无同轮直接调用对照、不属"每条消息"式
 *  场景预算），因此只记录一次读数并按 `ungraded` 呈现，不出六档评级（详见 id_cmake_performance.cpp）。
 *  `id.cli` 子域只断言用法、退出码与生成/`--check` 幂等，不计耗时——CLI 进程启动成本未列入指标。
 */

namespace ID::Bench
{
    void IdPerformanceTest() noexcept
    {
        IdCmakePerformanceTest();
    }

    void IdPerformanceNotes() noexcept
    {
        IdCmakePerformanceNotes();
        ReportLine("    - 子域覆盖：本区只有 id.cmake 的工具耗时指标；id.cli 子域不计耗时，"
                   "其进程启动成本未列入指标（不作为 0 报告）");
    }
} // namespace ID::Bench
