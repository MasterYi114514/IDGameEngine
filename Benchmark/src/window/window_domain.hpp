#pragma once

#include <cstddef>

/**
 *  IDWindow 测试域的声明与构建期注入项兜底。
 *
 *  域声明只有函数声明（与 framework.md §1 的"域声明"职责一致）；声明文件放在 `src/window/`
 *  而不是 `include/benchmark_idwindow.hpp`，理由见结果文件 ⑤：本卡 `writes` 未包含
 *  `Benchmark/include/`，为避免在 `include/` 下新增第二套并行约定，声明跟随实现放在本目录，
 *  由主入口显式包含并登记。
 *
 *  构建期注入项（CMake 注入；缺失时按下列兜底显示，不猜值）：
 *  - `BENCHMARK_CXX_COMPILER`     当前 C++ 编译器绝对路径（编译器级负例取证用）
 *  - `BENCHMARK_CXX_FAMILY`       `MSVC` 或 `GNU`/`Clang`（决定负例的命令行形态）
 *  - `BENCHMARK_WINDOW_SRC_DIR`   `<benchmark>/src`（负例源与探针 TU 的定位根）
 *  - `BENCHMARK_IDWINDOW_INCLUDE` 被测插件的公开 include 根
 *  - `BENCHMARK_COMMON_INCLUDE`   `Common` 复用头的 include 根（公开头依赖 `IDHandle.hpp`/`Core/Core.hpp`）
 */

#if !defined(BENCHMARK_CXX_COMPILER)
    #define BENCHMARK_CXX_COMPILER "g++"
#endif

#if !defined(BENCHMARK_CXX_FAMILY)
    #define BENCHMARK_CXX_FAMILY "unknown"
#endif

#if !defined(BENCHMARK_WINDOW_SRC_DIR)
    #define BENCHMARK_WINDOW_SRC_DIR "."
#endif

#if !defined(BENCHMARK_IDWINDOW_INCLUDE)
    #define BENCHMARK_IDWINDOW_INCLUDE "."
#endif

#if !defined(BENCHMARK_COMMON_INCLUDE)
    #define BENCHMARK_COMMON_INCLUDE "."
#endif

namespace ID::Bench
{
    /// @brief IDWindow 正确性用例（契约一致性、边界、非法输入、状态与幂等、隔离与事件、GL/Vulkan Win32 面）
    void IdWindowCorrectnessTest() noexcept;

    /// @brief IDWindow 接入成本与转发路径成本用例（与"直接 GLFW 等价接线"同轮对照）
    void IdWindowPerformanceTest() noexcept;

    /// @brief 性能区的统计口径、参考值推导与覆盖局限说明（在指标表之后输出，不混入指标行）
    void IdWindowPerformanceNotes() noexcept;
} // namespace ID::Bench
