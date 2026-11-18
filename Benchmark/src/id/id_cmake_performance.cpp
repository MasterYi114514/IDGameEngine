#include <cstddef>
#include <optional>
#include <string>

#include "benchmark_id.hpp"

#include "benchmark_support.hpp"
#include "cmake/Directory/Directory.hpp"
#include "cmake/Directory/DirectoryEmitter.hpp"
#include "cmake/Project/Project.hpp"
#include "cmake/Target/TargetType.hpp"

/**
 *  id.cmake 性能区：`ID cmake` 的生成流水线耗时（工具耗时，不是游戏性能）。
 *
 *  测什么：把一个代表工程的生成流水线跑一遍——`validate(project)` + `emit_directory(root)`
 *  （后者内部产出全部目标块）。这是工具侧"生成文本"的整条成功路径，输入固定、无 IO（不落盘）、无并行。
 *
 *  怎么测：先标定（从 8 次迭代起翻倍到整批 ≥20 ms，用该批的 ns/op 反算迭代数，使单轮 ≈150 ms，
 *  满足 methodology §2 的 ≥100 ms），再预热一轮不计时，然后固定 5 轮计时并报中位数；极差与迭代数写进指标 note。
 *  标定批不得只有亚毫秒级：那样一次调度抢占就能把 ns/op 抬高数倍，反算出过短的计时轮。
 *  防优化：每轮把返回值累加进 checksum，循环外经跨 TU 的 `KeepAlive` 写入 sink；
 *  计时区间内不做 `Check`、不打印、不做文件 IO。
 *
 *  为什么不评级：工具耗时没有可核实依据——既没有同轮同输入的直接调用对照（整条流水线就是被测对象本身），
 *  也不属于"每条消息"式的场景预算（grading.md §2 E 把 budget 限定在同类的逐条操作上）。
 *  因此本次只记录一次读数、`basis` 留空按 `ungraded` 呈现，不出六档评级；`ungraded` 只影响退出码位 1。
 *
 *  局限：读数只代表本报告头所示工具链/配置；Debug 变体含大量未优化代码，与 Release 的读数不可互换。
 */

namespace
{
    using ID::Bench::Check;
    using ID::Bench::FormatDouble;
    using ID::Bench::KeepAlive;
    using ID::Bench::RecordMetric;
    using ID::Bench::ReportLine;
    using ID::Bench::SampleStats;
    using ID::Bench::Summarize;
    using ID::Bench::Timer;
    using ID::CMake::Argument;
    using ID::CMake::Directory;
    using ID::CMake::Project;
    using ID::CMake::Scope;
    using ID::CMake::TargetType;

    // 单轮目标时长：methodology §2 要求单轮 ≥100 ms，取 150 ms 留余量（预热后速度会变快，故按预热后速度重算）
    constexpr std::size_t kRoundTargetNs = 150'000'000;

    // 迭代数上限：避免流水线极快时把单轮拉成数秒（读数仍然有效，只是单轮更短）
    constexpr std::size_t kMaxIterations = 20'000;

    // 固定 5 轮（methodology §2）
    constexpr int kRounds = 5;

    // 代表模型：与官方生成的四份插件文件同构——project 语言、option、变量、带条件的全局编译选项、
    // 形态可切换库目标（互斥宏块 + 同条件普通条目）、可执行目标、INTERFACE 库、跨目录链接与子目录
    void BuildRepresentativeProject(Project& project)
    {
        Directory& root = project.root();
        root.add_language(Argument::bare("CXX"));
        root.add_option(Argument::bare("BENCH_SHARED"), false, Argument::quoted("基准构建共享库"));
        root.add_variable(Argument::bare("BENCH_OUTPUT_DIR"),
                          {Argument::quoted_expand("${CMAKE_SOURCE_DIR}/bin")},
                          false,
                          Argument::quoted(""));
        root.add_variable(Argument::bare("BENCH_FEATURES"), {Argument::bare("ON")}, true, Argument::quoted("特性开关"));
        root.add_global_compile_option(Argument::bare("-Wall"));
        root.add_global_compile_option(Argument::bare("-Wextra"), Argument::verbatim("MINGW"));
        root.add_global_compile_option(Argument::bare("-Wpedantic"), Argument::verbatim("MINGW"));
        root.add_configuration_command(Argument::verbatim("message(STATUS \"benchmark config\")"));

        project.add_external_target(Argument::bare("IDLog"));
        project.add_external_target(Argument::bare("IDCommonHeaders"));

        ID::CMake::Target& library = root.create_target("BenchCore", TargetType::StaticLibrary);
        library.set_source_glob_dir("src");
        library.set_output_directory(Argument::quoted_expand("${CMAKE_SOURCE_DIR}/bin"));
        library.set_form_option(Argument::bare("BENCH_SHARED"),
                                Argument::bare("BENCH_SHARED_BUILD"),
                                Argument::bare("BENCH_STATIC_BUILD"));
        library.add_compile_definition(Scope::Public, Argument::bare("BENCH_API"));
        library.add_compile_definition(Scope::Private, Argument::bare("BENCH_SHARED_MARK"), Argument::verbatim("BENCH_SHARED"));
        library.add_include_directory(Scope::Public, Argument::quoted_expand("${CMAKE_CURRENT_SOURCE_DIR}/include"));
        library.add_include_directory(Scope::Private, Argument::quoted_expand("${CMAKE_SOURCE_DIR}/vendor/include"),
                                      std::nullopt, true);
        library.add_link_directory(Scope::Private, Argument::quoted_expand("${CMAKE_SOURCE_DIR}/lib"));
        library.add_link_library(Scope::Public, Argument::bare("IDCommonHeaders"));
        library.add_link_library(Scope::Private, Argument::bare("IDLog"));

        ID::CMake::Target& tool = root.create_target("BenchTool", TargetType::Executable);
        tool.set_source_glob_dir("src");
        tool.add_compile_definition(Scope::Private, Argument::bare("BENCH_TOOL"));
        tool.add_link_library(Scope::Private, Argument::bare("BenchCore"));

        ID::CMake::Target& headers = root.create_target("BenchHeaders", TargetType::InterfaceLibrary);
        headers.add_include_directory(Scope::Interface, Argument::quoted_expand("${CMAKE_SOURCE_DIR}/include"));
        headers.add_compile_definition(Scope::Interface, Argument::bare("BENCH_HEADERS"));

        Directory& plugin = root.create_subdirectory("Plugin");
        ID::CMake::Target& plugin_target = plugin.create_target("BenchPlugin", TargetType::StaticLibrary);
        plugin_target.set_source_glob_dir("src");
        plugin_target.add_link_library(Scope::Public, Argument::bare("BenchHeaders"));
        plugin_target.add_link_library(Scope::Private, Argument::bare("IDLog"));
        root.create_referenced_subdirectory("Vendor");
        root.add_trailing_command(Argument::verbatim("message(STATUS \"benchmark tail\")"));
    }

    // 一次完整生成流水线：校验 + 整文件排版（含全部目标块）。返回值由调用方累加进 checksum，防优化。
    ID_NOINLINE std::size_t GenerateOnce(const Project& project)
    {
        const ID::CMake::ValidationReport report = ID::CMake::validate(project);
        const std::string                text   = ID::CMake::emit_directory(project.root());
        return report.diagnostics.size() + text.size();
    }

    // 标定窗口下限：一次标定批至少要跑这么久才拿它的 ns/op 反算迭代数。稳态单次耗时约 80 µs 时，8 次迭代的
    // 标定批只有约 0.6 ms，一次调度抢占就能把 ns/op 抬高数倍，反算出过小的迭代数，使计时轮短于 100 ms
    // （实测出现过 iters=546、单轮 43.1 ms 而稳态中位数仍为 79 µs 的情形）。
    constexpr double kCalibrationFloorNs = 20'000'000.0;

    // 标定翻倍次数上限：批大小最多到 8 << 7 = 1024 次迭代，足够让整批耗时越过下限
    constexpr int kCalibrationDoublings = 8;

    /// @brief 把实测单次耗时换算成"单轮接近 kRoundTargetNs"的迭代数（下限 1、上限 kMaxIterations）
    std::size_t ClampIterations(double per_operation_ns) noexcept
    {
        if (!(per_operation_ns > 0.0))
        {
            return kMaxIterations;
        }

        const double wanted = static_cast<double>(kRoundTargetNs) / per_operation_ns;
        if (wanted < 1.0)
        {
            return 1;
        }
        if (wanted > static_cast<double>(kMaxIterations))
        {
            return kMaxIterations;
        }
        return static_cast<std::size_t>(wanted);
    }

    /// @brief 跑一批 GenerateOnce 并返回单次耗时（ns/op）；checksum 用于跨 TU 的防优化 sink
    double MeasureBatch(const Project& project, std::size_t iterations, std::size_t& checksum)
    {
        const Timer timer;
        for (std::size_t index = 0; index < iterations; ++index)
        {
            checksum += GenerateOnce(project);
        }
        return timer.ElapsedNs() / static_cast<double>(iterations);
    }

    /**
     *  标定迭代数：从 8 次迭代起按整批耗时翻倍，直到整批 ≥ kCalibrationFloorNs（或到批大小上限），
     *  再用该批的 ns/op 反算"单轮接近 kRoundTargetNs"的迭代数。
     *  为什么不用固定小批：见 kCalibrationFloorNs 的注释——短批会因一次抢占把 ns/op 抬高数倍。
     */
    std::size_t CalibrateIterations(const Project& project, std::size_t& checksum)
    {
        std::size_t batch           = 8;
        double      per_operation_ns = 0.0;
        for (int attempt = 0; attempt < kCalibrationDoublings; ++attempt)
        {
            const Timer timer;
            for (std::size_t index = 0; index < batch; ++index)
            {
                checksum += GenerateOnce(project);
            }
            const double elapsed_ns = timer.ElapsedNs();
            per_operation_ns = elapsed_ns / static_cast<double>(batch);
            if (elapsed_ns >= kCalibrationFloorNs)
            {
                break;
            }
            batch *= 2;
        }
        KeepAlive(&checksum);
        return ClampIterations(per_operation_ns);
    }
} // namespace

namespace ID::Bench
{
    void IdCmakePerformanceTest() noexcept
    {
        Project project("BenchCmake");
        BuildRepresentativeProject(project);

        // 前置不变量（在计时之外）：代表模型必须通过生成前校验，否则测到的耗时不代表成功路径
        const ID::CMake::ValidationReport report = ID::CMake::validate(project);
        Check(report.is_valid, "id.cmake.tool.pipeline.model_valid",
              "代表工程的 validate 必须通过（失败说明模型或校验口径有问题，耗时不代表成功路径）");
        const std::string text = ID::CMake::emit_directory(project.root());
        Check(!text.empty(), "id.cmake.tool.pipeline.text_non_empty", "emit_directory 必须产出非空文本");

        std::size_t checksum = 0;

        // 第一次标定（冷态）：只用来安排预热批大小
        std::size_t iterations = CalibrateIterations(project, checksum);
        KeepAlive(&checksum);

        // 预热：一轮完整迭代，不计时、不记样本（让频率与缓存进入稳定状态）
        MeasureBatch(project, iterations, checksum);
        KeepAlive(&checksum);

        // 预热后重新标定：methodology §2 要求单轮 ≥100 ms，用计时阶段的实际速度反算迭代数
        iterations = CalibrateIterations(project, checksum);
        KeepAlive(&checksum);

        double samples[kRounds] = {};
        for (int round = 0; round < kRounds; ++round)
        {
            samples[round] = MeasureBatch(project, iterations, checksum);
            KeepAlive(&checksum);
        }

        const SampleStats stats = Summarize(samples, kRounds);
        Check(stats.median > 0.0, "id.cmake.tool.pipeline.median_positive", "中位耗时为正值（计时源与迭代数有效）");
        KeepAlive(&checksum);

        // 单轮时长门槛：够长才不让调度噪声主导（迭代数被上限截断时无法保证，按环境降级登记）
        const double round_ns = stats.median * static_cast<double>(iterations);
        if (iterations < kMaxIterations)
        {
            Check(round_ns >= 100.0e6, "id.cmake.tool.pipeline.round_long_enough",
                  "单轮时长 ≥100 ms（methodology §2）；实际 ≈ " + FormatDouble(round_ns / 1.0e6, 1) + " ms");
        }
        else
        {
            ID::Bench::Warn("id.cmake.tool.pipeline.round_capped",
                            "迭代数达到上限 " + std::to_string(kMaxIterations) + "，单轮 ≈ "
                                + FormatDouble(round_ns / 1.0e6, 1) + " ms < 100 ms：读数只作数量级参考");
        }

        // 极差超过 20% 噪声带：读数仍如实记录，但只作数量级参考（basis 已留空，因此不影响评级口径）
        if (stats.is_unstable)
        {
            ID::Bench::Warn("id.cmake.tool.pipeline.unstable",
                            "5 轮极差 " + FormatDouble(stats.spread * 100.0, 1) + "% 超过 20% 噪声带："
                            "本题材为 Debug 构建的分配与调度抖动，读数只作数量级参考");
        }

        // 工具耗时只记录一次：无同轮对照、也不属"每条消息"式场景预算 → basis 留空 → ungraded，不出评级
        const std::string note = std::string("工具耗时（ID cmake 生成流水线 = validate + emit_directory 整文件排版），"
                                             "不是游戏性能，不出评级；iters=")
                               + std::to_string(iterations) + "，单轮≈"
                               + FormatDouble(static_cast<double>(iterations) * stats.median / 1.0e6, 1) + " ms；min="
                               + FormatDouble(stats.minimum, 1) + " ns max=" + FormatDouble(stats.maximum, 1)
                               + " ns spread=" + FormatDouble(stats.spread * 100.0, 1) + "%；"
                               + "basis 留空（无同轮直接调用对照、无场景预算假设），按 ungraded 记录";
        RecordMetric({
            .name         = "id.cmake.generate_pipeline",
            .actual_ns    = stats.median,
            .reference_ns = 0.0,
            .basis        = {},
            .note         = note,
        });
    }

    void IdCmakePerformanceNotes() noexcept
    {
        ReportLine("    - 统计口径（多轮）：预热一轮后按实测单次耗时反算迭代数，使单轮 ≈150 ms"
                   "（methodology §2 要求单轮 ≥100 ms，代码内有单轮时长断言）；标定批从 8 次迭代翻倍到整批 ≥20 ms"
                   "才用于反算，避免亚毫秒级短批被一次调度抢占抬高 ns/op、把计时轮压到 100 ms 以下；"
                   "固定 5 轮计时、报中位数，note 给出 iters / 单轮 ms / min / max / spread；"
                   "5 轮极差超 20% 时用 [WARN] 标注只作数量级参考。");
        ReportLine("    - 被测操作：`validate(project)` + `emit_directory(root)` 一次（emit_directory 内部产出全部目标块，"
                   "覆盖骨架顺序、作用域分段、条件 run、形态选项两段与第 13 项）。");
        ReportLine("    - 固定输入：代表工程 = project 语言 + option + 普通/CACHE 变量 + 带条件的全局编译选项 +"
                   " 形态可切换静态库（编译宏/include（含 SYSTEM）/链接目录/链接库）+ 可执行目标 + INTERFACE 库 +"
                   " 子目录目标 + referenced 子目录 + 尾部命令；输入固定，换模型即视为另一个测试。");
        ReportLine("    - 防优化：每轮把返回值累加进 checksum，循环外经跨 TU 的 KeepAlive 写入 sink；"
                   "计时区间内不做 Check、不打印、不做文件 IO、不落盘。");
        ReportLine("    - 参考值口径：本项 basis 留空 → ungraded（依据列 n/a [-]）。理由：工具耗时没有同轮同输入的直接"
                   "调用对照（整条流水线就是被测对象本身），也不属于 grading.md §2 E 限定的『每条消息』式场景预算，"
                   "因此不给六档评级；ungraded 只置退出码位 1，不改变用例判定。");
        ReportLine("    - 未列入指标（methodology §7 允许单独说明）：FragmentWriter 的落盘 IO（含临时文件与改名替换）、"
                   "自描述生成（SelfCmake 构造四份模型）与 CLI 进程启动成本（id.cli 子域只断言用法、退出码与"
                   "生成/--check 幂等，不计耗时）；这些不进本表，也不计入插件性能。");
        ReportLine("    - 工具耗时不是游戏性能：本子域结论只描述『ID cmake 生成文本』的工具成本，"
                   "不得与 IDLog/IDJson/IDWindow 的插件自身成本或接入成本合并或比较。");
        ReportLine("    - 未验证组合：本报告只覆盖报告头 compiler/build 行所示的工具链与配置（GCCBuild = MinGW GCC / "
                   "Debug）；MSVC 变体（Debug / 32 位 / SHARED）与 Linux 上的生成流水线耗时均未测量，"
                   "不能说成通过或等价。");
    }
} // namespace ID::Bench
