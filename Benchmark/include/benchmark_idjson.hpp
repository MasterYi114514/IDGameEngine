#pragma once

/**
 *  IDJson 测试域声明（只有该域的函数声明，不含实现）。
 *
 *  被测对象：`IDPlugin/IDJson` 的公开契约（六个公开头 + README）；以普通库方式消费
 *  （公开头 + CMake 目标 `IDJson`），不经 `ID` 的生成接线，也不依赖运行期服务槽。
 *
 *  域内的正确性/性能用例分别在 `src/idjson/idjson_correctness.cpp` 与
 *  `src/idjson/idjson_performance.cpp`；共享的测试数据、段来源计数分配器与 DOM 指纹
 *  在私有头 `src/idjson/idjson_testdata.hpp`（不进公开 include 路径）。
 */

namespace ID::Bench
{
    void IdJsonCorrectnessTest() noexcept;
    void IdJsonPerformanceTest() noexcept;

    /// @brief 性能区的统计口径、预算推导与覆盖局限说明（在指标表之后输出，不混入指标行）
    void IdJsonPerformanceNotes() noexcept;
} // namespace ID::Bench
