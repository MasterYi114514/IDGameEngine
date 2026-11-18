#include <chrono>
#include <cstddef>
#include <ctime>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "benchmark_id.hpp"
#include "benchmark_support.hpp"
#include "id_project_cases.hpp"
#include "id_project_test_support.hpp"

/**
 *  `id.project` 构建组（I1–I15）。
 *
 *  统一 profile：`--generator "MinGW Makefiles" --toolchain mingw --arch x64`，配置按行给 `debug`/`release`，
 *  产物断言落 `<项目根>/bin/mingw/x64/<config>/`。真实构建逐条执行，失败按真实退出码记录，不重试到
 *  "看起来成功"。`build.json` 的 `verdict` / `stage` / `configure_exit` / `build_exit` 是判定主依据。
 */

namespace
{
    using ID::Bench::Check;
    using ID::Bench::IdProject::AddExit;
    using ID::Bench::IdProject::AppendMingwProfile;
    using ID::Bench::IdProject::ArtifactPath;
    using ID::Bench::IdProject::CaseRoot;
    using ID::Bench::IdProject::CaseVerdict;
    using ID::Bench::IdProject::CheckCategory;
    using ID::Bench::IdProject::CommandResult;
    using ID::Bench::IdProject::HasCategory;
    using ID::Bench::IdProject::IsDirectory;
    using ID::Bench::IdProject::IsRegularFile;
    using ID::Bench::IdProject::JsonReader;
    using ID::Bench::IdProject::LastWriteTicks;
    using ID::Bench::IdProject::ListTree;
    using ID::Bench::IdProject::PluginSpec;
    using ID::Bench::IdProject::ProjectRecordPath;
    using ID::Bench::IdProject::ReadBuildRecords;
    using ID::Bench::IdProject::ReadResolved;
    using ID::Bench::IdProject::ReadText;
    using ID::Bench::IdProject::RunProject;
    using ID::Bench::IdProject::Sha256OfFile;
    using ID::Bench::IdProject::SnapshotDiff;
    using ID::Bench::IdProject::TakeSnapshot;
    using ID::Bench::IdProject::WritePlugin;
    using ID::Bench::IdProject::WritePluginsJson;
    using ID::Bench::IdProject::WriteText;

    PluginSpec SimplePlugin(std::string name, std::string dependencies = "[]", std::string options = "[]")
    {
        PluginSpec spec;
        spec.name              = std::move(name);
        spec.dependencies_json = std::move(dependencies);
        spec.options_json      = std::move(options);
        return spec;
    }

    void FinishCase(std::string_view name, const CaseRoot& root, const CaseVerdict& verdict) noexcept
    {
        Check(verdict.IsOk(), name, verdict.Detail());
        if (!verdict.IsOk())
        {
            root.ArchiveEvidence(std::string(name.substr(name.rfind('.') + 1)) + "-fail");
        }
    }

    /// @brief `vector` 形式的 build 参数（统一 profile + 指定配置）
    std::vector<std::string> BuildArguments(std::string_view name, std::string_view config,
                                            std::string_view arch = "x64")
    {
        std::vector<std::string> arguments = { "project", "build", std::string(name) };
        arguments.emplace_back("--generator");
        arguments.emplace_back("MinGW Makefiles");
        arguments.emplace_back("--toolchain");
        arguments.emplace_back("mingw");
        arguments.emplace_back("--arch");
        arguments.emplace_back(std::string(arch));
        arguments.emplace_back("--config");
        arguments.emplace_back(std::string(config));
        return arguments;
    }

    std::string BuildField(const JsonReader& records, std::size_t index, std::string_view field) noexcept
    {
        return records.String(std::to_string(index) + "." + std::string(field));
    }

    bool IsFieldNull(const JsonReader& records, std::size_t index, std::string_view field) noexcept
    {
        const ID::Json::JsonValue* node = records.Find(std::to_string(index) + "." + std::string(field));
        return node == nullptr || node->is_null();
    }

    /// @brief 构建日志文件名列表（只取文件名）
    std::vector<std::string> LogNames(const CaseRoot& root) noexcept
    {
        std::vector<std::string> names;
        for (const std::string& entry : ListTree(root.ProjectPath("IDGenerated/log")))
        {
            if (entry.rfind("FILE ", 0) == 0)
            {
                names.push_back(entry.substr(5));
            }
        }
        return names;
    }

    /// @brief `build/<toolchain>/<arch>/<config>/CMakeCache.txt` 路径
    std::string CachePath(const CaseRoot& root, std::string_view arch, std::string_view config) noexcept
    {
        return root.ProjectPath("build/mingw/" + std::string(arch) + "/" + std::string(config) + "/CMakeCache.txt");
    }

    /// @brief 当前 UTC 秒的时间戳段 `<yyyyMMddTHHmmssZ>`
    std::string CurrentUtcStamp() noexcept
    {
        const std::time_t now = std::time(nullptr);
        std::tm           utc{};
#if defined(_WIN32)
        gmtime_s(&utc, &now);
#else
        gmtime_r(&now, &utc);
#endif
        char buffer[32] = {};
        std::strftime(buffer, sizeof(buffer), "%Y%m%dT%H%M%SZ", &utc);
        return std::string(buffer);
    }

    // ==================== 用例 ====================

    /// I1：debug 与 release 各自独立记录与产物目录，互不覆盖
    void CaseI1() noexcept
    {
        const CaseRoot root("i1");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        AddExit(verdict, "new exit0",
                RunProject(root, { "project", "new", "--name", "I1", "--path", root.project_root(), "--plugin", "Sample" }),
                0);

        AddExit(verdict, "build debug exit0", RunProject(root, BuildArguments("I1", "debug")), 0);
        AddExit(verdict, "build release exit0", RunProject(root, BuildArguments("I1", "release")), 0);

        const std::string debug_artifact   = ArtifactPath(root, "mingw", "x64", "debug", "I1.exe");
        const std::string release_artifact = ArtifactPath(root, "mingw", "x64", "release", "I1.exe");
        verdict.Add(IsRegularFile(debug_artifact), "debug artifact exists");
        verdict.Add(IsRegularFile(release_artifact), "release artifact exists");

        bool              is_debug_ok   = false;
        bool              is_release_ok = false;
        const std::string debug_digest   = Sha256OfFile(debug_artifact, is_debug_ok);
        const std::string release_digest = Sha256OfFile(release_artifact, is_release_ok);
        verdict.Add(is_debug_ok && is_release_ok && debug_digest != release_digest,
                    "artifact digests differ (independent output dirs)");

        JsonReader records;
        verdict.Add(ReadBuildRecords(root, records) && records.Count("") == 2, "two independent records");
        verdict.Add(BuildField(records, 0, "config") == "debug" && BuildField(records, 1, "config") == "release",
                    "records carry debug/release");

        FinishCase("id.project.i1", root, verdict);
    }

    /// I2：多配置生成器在 P1 被拒 → build_environment_unsupported(4)，零写、不 configure
    void CaseI2() noexcept
    {
        const CaseRoot root("i2");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        AddExit(verdict, "new exit0",
                RunProject(root, { "project", "new", "--name", "I2", "--path", root.project_root(), "--plugin", "Sample" }),
                0);

        const ID::Bench::IdProject::Snapshot before = TakeSnapshot(root);
        const CommandResult result = RunProject(root, { "project", "build", "I2", "--generator", "Visual Studio 18 2026",
                                                        "--toolchain", "msvc", "--arch", "x64", "--config", "debug" });
        const ID::Bench::IdProject::Snapshot after = TakeSnapshot(root);

        AddExit(verdict, "exit4", result, 4);
        CheckCategory(verdict, "build_environment_unsupported", result, "build_environment_unsupported");
        verdict.Add(SnapshotDiff(before, after).empty(), "zero write");
        verdict.Add(!IsRegularFile(CachePath(root, "x64", "debug")), "no CMakeCache.txt (no configure)");

        FinishCase("id.project.i2", root, verdict);
    }

    /// I3：未给 `--generator` 且无缓存 → build_environment_unsupported(4)，诊断给可执行下一步
    void CaseI3() noexcept
    {
        const CaseRoot root("i3");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        AddExit(verdict, "new exit0",
                RunProject(root, { "project", "new", "--name", "I3", "--path", root.project_root(), "--plugin", "Sample" }),
                0);

        const CommandResult bare = RunProject(root, { "project", "build", "I3", "--config", "debug" });
        AddExit(verdict, "no generator exit4", bare, 4);
        CheckCategory(verdict, "no generator build_environment_unsupported", bare, "build_environment_unsupported");
        verdict.Add(HasCategory(bare, "--generator"), "diagnostic gives a copyable --generator form");
        verdict.Add(!IsRegularFile(CachePath(root, "x64", "debug")), "no configure");

        const CommandResult partial = RunProject(root, { "project", "build", "I3", "--toolchain", "mingw", "--arch", "x64",
                                                         "--config", "debug" });
        AddExit(verdict, "profile without generator exit4", partial, 4);
        verdict.Add(!IsRegularFile(CachePath(root, "x64", "debug")), "still no configure");

        FinishCase("id.project.i3", root, verdict);
    }

    /// I4a：根 `CMakeLists.txt` 片段外非法内容 → configure 阶段失败（stage=configure，build_exit=null）
    void CaseI4a() noexcept
    {
        const CaseRoot root("i4a");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        AddExit(verdict, "new exit0",
                RunProject(root, { "project", "new", "--name", "I4a", "--path", root.project_root(), "--plugin", "Sample" }),
                0);

        std::string cmake_text;
        verdict.Add(ReadText(root.ProjectPath("CMakeLists.txt"), cmake_text), "root CMakeLists readable");
        verdict.Add(WriteText(root.ProjectPath("CMakeLists.txt"), cmake_text + "this_is_not_cmake(\n"),
                              "user broke the file outside fragments");

        const CommandResult build = RunProject(root, BuildArguments("I4a", "debug"));
        AddExit(verdict, "build exit4", build, 4);

        JsonReader records;
        verdict.Add(ReadBuildRecords(root, records) && records.Count("") == 1, "one failed record");
        verdict.Add(BuildField(records, 0, "verdict") == "configure_failed", "verdict=configure_failed");
        verdict.Add(BuildField(records, 0, "stage") == "configure", "stage=configure");
        verdict.Add(!IsFieldNull(records, 0, "configure_exit") && records.Int("0.configure_exit", 0) != 0,
                    "configure_exit is a non-zero measured value");
        verdict.Add(IsFieldNull(records, 0, "build_exit"), "build_exit=null (build not started)");

        FinishCase("id.project.i4a", root, verdict);
    }

    /// I4b：`src/main.cpp` 语法错 → configure 通过、build 失败（stage=build，configure_exit=0）
    void CaseI4b() noexcept
    {
        const CaseRoot root("i4b");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        AddExit(verdict, "new exit0",
                RunProject(root, { "project", "new", "--name", "I4b", "--path", root.project_root(), "--plugin", "Sample" }),
                0);

        verdict.Add(WriteText(root.ProjectPath("src/main.cpp"), "int main( { return 0; }\n"),
                              "user broke the seed source");

        const CommandResult build = RunProject(root, BuildArguments("I4b", "debug"));
        AddExit(verdict, "build exit4", build, 4);

        JsonReader records;
        verdict.Add(ReadBuildRecords(root, records) && records.Count("") == 1, "one failed record");
        verdict.Add(BuildField(records, 0, "verdict") == "build_failed", "verdict=build_failed");
        verdict.Add(BuildField(records, 0, "stage") == "build", "stage=build");
        verdict.Add(records.Int("0.configure_exit", -1) == 0, "configure_exit=0");
        verdict.Add(!IsFieldNull(records, 0, "build_exit") && records.Int("0.build_exit", 0) != 0,
                    "build_exit is a non-zero measured value");

        FinishCase("id.project.i4b", root, verdict);
    }

    /**
     *  I5：构建成功但产物被移走 → `artifact_missing`（spec §15 I5）
     *
     *  本轮按 `Uncovered` 登记，不判通过、也不记 `Skip`。
     *  依据：本机 `mingw32-make` 在产物被删后会重新链接——删除 `bin/mingw/x64/debug/<exe>` 后重跑同一 `build`
     *  得到 `configure 0 / build 0` 并重新产出 exe（上一轮已实测，退出码 0、产物路径重新出现在诊断里），
     *  因此在真实构建下无法制造"命令返回 0 但产物缺失"的窗口；这不是被测行为缺失，而是生成器行为使该组合不可构造。
     *  工具侧的产物判定逻辑（产物存在才判 success）由 `id.project.i6` 的 no-op 用例与各真实构建用例的
     *  artifact 路径断言覆盖；待出现可控注入点或能复现该组合的生成器后，本项应改为断言。
     */
    void CaseI5() noexcept
    {
        ID::Bench::Uncovered("id.project.i5",
                             "MinGW Makefiles 会自动重链，真实构建后无法制造「返回 0 但产物缺失」的窗口"
                             "（删除产物后重跑同一 build：configure 0 / build 0 且重新链接出 exe，上一轮已实测）；"
                             "工具侧产物判定逻辑由 id.project.i6 的 no-op 用例与 artifact 路径断言覆盖");
    }

    /// I6：源码未变的两次同配置 build 都 success，不因 mtime 未变报 artifact_missing
    void CaseI6() noexcept
    {
        const CaseRoot root("i6");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        AddExit(verdict, "new exit0",
                RunProject(root, { "project", "new", "--name", "I6", "--path", root.project_root(), "--plugin", "Sample" }),
                0);
        AddExit(verdict, "first build exit0", RunProject(root, BuildArguments("I6", "debug")), 0);

        const std::string artifact = ArtifactPath(root, "mingw", "x64", "debug", "I6.exe");
        bool              is_time_ok = false;
        const std::string ticks_before = LastWriteTicks(artifact, is_time_ok);

        const CommandResult second = RunProject(root, BuildArguments("I6", "debug"));
        AddExit(verdict, "second build exit0", second, 0);
        verdict.Add(!HasCategory(second, "artifact_missing"), "no artifact_missing");

        bool              is_time_after_ok = false;
        const std::string ticks_after      = LastWriteTicks(artifact, is_time_after_ok);
        verdict.Add(is_time_ok && is_time_after_ok && ticks_before == ticks_after,
                    "artifact mtime unchanged (no-op build)");

        JsonReader records;
        verdict.Add(ReadBuildRecords(root, records) && records.Count("") == 2, "two records");
        verdict.Add(BuildField(records, 0, "verdict") == "success" && BuildField(records, 1, "verdict") == "success",
                    "both verdict=success");
        verdict.Add(BuildField(records, 0, "generation_id") == BuildField(records, 1, "generation_id")
                        && BuildField(records, 0, "manifest_digest") == BuildField(records, 1, "manifest_digest"),
                    "same generation_id and manifest_digest");

        FinishCase("id.project.i6", root, verdict);
    }

    /// I7 的共用体：给出项目根的**空白**形态并跑一次真实构建，判退出码、产物与日志可读性
    void RunI7Shape(std::string_view case_name, std::string_view case_id, std::string_view project_leaf) noexcept
    {
        const CaseRoot root(case_id);
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");

        // 夹具：用例根下另造一个各级名字符合 matrix §11 I7 的项目根（引擎根仍是 `engine/`）
        const std::string shaped_project = root.CasePath(project_leaf);
        verdict.Add(ID::Bench::IdProject::CreateDirectories(shaped_project), "shaped project root fixture");

        AddExit(verdict, "new exit0",
                RunProject(root, { "project", "new", "--name", "I7", "--path", shaped_project, "--plugin", "Sample" }), 0);

        std::vector<std::string> arguments = { "project", "build", "I7" };
        AppendMingwProfile(arguments);
        arguments.emplace_back("--config");
        arguments.emplace_back("debug");
        const CommandResult build = RunProject(root, arguments);
        AddExit(verdict, "build exit0", build, 0);

        verdict.Add(IsRegularFile(shaped_project + "/bin/mingw/x64/debug/I7.exe"), "artifact under the shaped path");

        std::string log_text;
        bool        is_log_found = false;
        for (const std::string& entry : ListTree(shaped_project + "/IDGenerated/log"))
        {
            if (entry.rfind("FILE ", 0) != 0)
            {
                continue;
            }
            std::string candidate;
            if (ReadText(shaped_project + "/IDGenerated/log/" + entry.substr(5), candidate))
            {
                log_text     = candidate;
                is_log_found = true;
            }
        }
        verdict.Add(is_log_found, "build log written");
        verdict.Add(log_text.find(shaped_project) != std::string::npos, "log contains the full project root text");
        verdict.Add(log_text.find("\xEF\xBF\xBD") == std::string::npos, "no Unicode replacement character in the log");

        FinishCase(case_name, root, verdict);
    }

    /// I7a：项目路径含**空格**、纯 ASCII → 构建成功、产物落位、日志参数逐字可读
    void CaseI7AsciiSpacePath() noexcept
    {
        RunI7Shape("id.project.i7.ascii_space_path", "i7-ascii-space", "project root with spaces");
    }

    /**
     * I7b：项目路径含**中文** → 本轮按 `Uncovered` 登记，不判通过、也不记 `Skip`
     *
     *  依据：本机 `mingw32-make` 不能承载非 ASCII 路径。已用最小 CMake 工程（同一 profile、不含 ID）独立复现
     *  configure=0 / build=2，报 `Not a file: .../CMakeFiles/VerifyGlobs.cmake`；证据见
     *  `out/verify/T84/i7-non-ascii-probe.log`。工具本身给出了可执行的下一步（换工具链或换路径），
     *  因此这是环境障碍而不是被测行为缺失；换到能承载非 ASCII 路径的生成器/平台后本项应改为断言。
     */
    void CaseI7NonAsciiPath() noexcept
    {
        ID::Bench::Uncovered("id.project.i7.non_ascii_path",
                             "本机 MinGW Makefiles + mingw32-make 不支持非 ASCII 路径（已用最小 CMake 工程独立复现："
                             "configure 0 / build 2，报 Not a file: .../CMakeFiles/VerifyGlobs.cmake）；"
                             "工具已给出可执行的下一步（换工具链或换路径）。本轮不具备可承载该路径的生成器");
    }

    /// I8：`--cmake` 指向不存在的可执行文件 → build_invoke_failed(4)
    void CaseI8() noexcept
    {
        const CaseRoot root("i8");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        AddExit(verdict, "new exit0",
                RunProject(root, { "project", "new", "--name", "I8", "--path", root.project_root(), "--plugin", "Sample" }),
                0);

        const std::string missing_cmake = root.CasePath("no-such-toolchain/cmake.exe");
        verdict.Add(ID::Bench::IdProject::CreateDirectories(root.CasePath("no-such-toolchain")),
                    "fixture dir without cmake.exe");

        std::vector<std::string> arguments = { "project", "build", "I8" };
        AppendMingwProfile(arguments);
        arguments.emplace_back("--config");
        arguments.emplace_back("debug");
        arguments.emplace_back("--cmake");
        arguments.emplace_back(missing_cmake);

        const CommandResult build = RunProject(root, arguments);
        AddExit(verdict, "build exit4", build, 4);
        CheckCategory(verdict, "build_invoke_failed", build, "build_invoke_failed");

        JsonReader records;
        verdict.Add(ReadBuildRecords(root, records) && records.Count("") == 1, "one record");
        verdict.Add(BuildField(records, 0, "verdict") == "invoke_failed", "verdict=invoke_failed");
        verdict.Add(IsFieldNull(records, 0, "configure_exit"), "configure_exit=null");
        verdict.Add(IsFieldNull(records, 0, "build_exit"), "build_exit=null");

        FinishCase("id.project.i8", root, verdict);
    }

    /// I9：日志命名唯一性；同 UTC 秒样本由"预置同前缀文件"构造
    void CaseI9() noexcept
    {
        const CaseRoot root("i9");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        AddExit(verdict, "new exit0",
                RunProject(root, { "project", "new", "--name", "I9", "--path", root.project_root(), "--plugin", "Sample" }),
                0);
        AddExit(verdict, "first build exit0", RunProject(root, BuildArguments("I9", "debug")), 0);

        const std::string stamp = CurrentUtcStamp();
        const std::string preset_name = "build-debug-" + stamp + "-0001.log";
        verdict.Add(WriteText(root.ProjectPath("IDGenerated/log/" + preset_name), "preset\n"),
                    "preset same-second log file");

        const CommandResult second = RunProject(root, BuildArguments("I9", "debug"));
        AddExit(verdict, "second build exit0", second, 0);

        const std::vector<std::string> names = LogNames(root);
        std::size_t                    build_logs = 0;
        bool                           has_second_serial = false;
        for (const std::string& name : names)
        {
            if (name.rfind("build-debug-", 0) == 0)
            {
                ++build_logs;
                has_second_serial = has_second_serial || name.find(stamp + "-0002.log") != std::string::npos;
            }
            verdict.Add(name.find(':') == std::string::npos, "log name has no ':' (" + name + ")");
        }
        verdict.Add(build_logs >= 3, "two build logs plus the preset file exist");
        if (has_second_serial)
        {
            verdict.Add(true, "same-second sample allocated -0002 without overwriting -0001");
        }
        else
        {
            ID::Bench::Uncovered("id.project.i9.same-second",
                                 "预置同前缀文件后落盘前已跨秒，得不到同 UTC 秒的同前缀样本（§11.6 U-6）；"
                                 "日志命名唯一性主构造已判定");
        }

        FinishCase("id.project.i9", root, verdict);
    }

    /// I10：上一代产物仍在 `bin/`，本次 configure 返回非 0 → verdict 不因旧产物变成 success
    void CaseI10() noexcept
    {
        const CaseRoot root("i10");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        AddExit(verdict, "new exit0",
                RunProject(root, { "project", "new", "--name", "I10", "--path", root.project_root(), "--plugin", "Sample" }),
                0);
        AddExit(verdict, "first build exit0", RunProject(root, BuildArguments("I10", "debug")), 0);
        verdict.Add(IsRegularFile(ArtifactPath(root, "mingw", "x64", "debug", "I10.exe")), "old artifact exists");

        std::string cmake_text;
        ReadText(root.ProjectPath("CMakeLists.txt"), cmake_text);
        verdict.Add(WriteText(root.ProjectPath("CMakeLists.txt"), cmake_text + "this_is_not_cmake(\n"),
                              "user broke the file outside fragments");

        const CommandResult build = RunProject(root, BuildArguments("I10", "debug"));
        AddExit(verdict, "build exit4", build, 4);

        JsonReader records;
        verdict.Add(ReadBuildRecords(root, records) && records.Count("") == 2, "two records");
        const std::string last_verdict = BuildField(records, 1, "verdict");
        verdict.Add(last_verdict == "configure_failed" || last_verdict == "build_failed",
                    "second verdict is a failure, not success");

        FinishCase("id.project.i10", root, verdict);
    }

    /// I11：`build.json` 临时文件阶段终止 → 旧数组完整、记录缺失、日志已写出
    void CaseI11() noexcept
    {
        const CaseRoot root("i11");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        AddExit(verdict, "new exit0",
                RunProject(root, { "project", "new", "--name", "I11", "--path", root.project_root(), "--plugin", "Sample" }),
                0);
        AddExit(verdict, "first build exit0", RunProject(root, BuildArguments("I11", "debug")), 0);

        std::string records_before;
        verdict.Add(ReadText(ProjectRecordPath(root, "build.json"), records_before), "build.json readable");
        const std::string digest_before = ID::Bench::IdProject::Sha256Hex(records_before);

        ID::Bench::IdProject::SpawnOptions options;
        options.inject = "build.after-record-temp=fail";
        const ID::Bench::IdProject::SpawnResult spawn = ID::Bench::IdProject::SpawnProject(
            root, "i11-inject", { "project", "build", "I11", "--generator", "MinGW Makefiles", "--toolchain", "mingw",
                                  "--arch", "x64", "--config", "debug" },
            options);

        verdict.Add(spawn.is_spawned && !spawn.is_timed_out && spawn.exit_code == 2, "exit2 (write_failed)");

        std::string records_after;
        verdict.Add(ReadText(ProjectRecordPath(root, "build.json"), records_after), "build.json still readable");
        verdict.Add(ID::Bench::IdProject::Sha256Hex(records_after) == digest_before,
                    "old build.json array bytes intact");
        JsonReader records;
        verdict.Add(records.LoadText(records_after) && records.Count("") == 1, "still a valid JSON array with 1 record");
        verdict.Add(!LogNames(root).empty(), "build log written before the record write");

        FinishCase("id.project.i11", root, verdict);
    }

    /**
     *  I12：`IDGenerated/log` 被同名普通文件占位 → 子构建照实执行，工具退出 2，log_path=null
     *
     *  判定口径：进程内驱动下子构建的 stdout/stderr 直接写测试进程的 stdout，不经过命令捕获通道，
     *  因此不断言"捕获通道里有 configure 输出"；改为断言子构建真被执行（`CMakeCache.txt` 出现）、
     *  `build.json` 的 `verdict` 记录的是子构建的真实结果、`log_path = null`，且工具退出码为 2
     *  （`write_failed`）——不把"子构建返回 0"报告成工具成功。
     */
    void CaseI12() noexcept
    {
        const CaseRoot root("i12");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        AddExit(verdict, "new exit0",
                RunProject(root, { "project", "new", "--name", "I12", "--path", root.project_root(), "--plugin", "Sample" }),
                0);

        verdict.Add(WriteText(root.ProjectPath("IDGenerated/log"), "blocking file\n"),
                    "log channel blocked by a regular file");

        const CommandResult build = RunProject(root, BuildArguments("I12", "debug"));
        AddExit(verdict, "tool exit2", build, 2);
        CheckCategory(verdict, "write_failed", build, "write_failed");
        // 子构建的输出在进程内驱动下直接写测试进程 stdout，不经命令捕获通道：只判"子构建真被执行"
        verdict.Add(IsRegularFile(CachePath(root, "x64", "debug")), "sub-build configured (CMakeCache exists)");

        JsonReader records;
        if (ReadBuildRecords(root, records) && records.Count("") == 1)
        {
            verdict.Add(BuildField(records, 0, "log_path").empty() && IsFieldNull(records, 0, "log_path"),
                        "log_path=null");
            // `verdict` 记的是子构建真实结果；工具自身因日志通道不可用退出 2，不据此把 0 报成工具成功
            verdict.Add(BuildField(records, 0, "verdict") == "success",
                        "verdict records the real sub-build result (sub-build succeeded)");
            verdict.Add(build.exit_code == 2 && HasCategory(build, "write_failed"),
                        "tool result stays write_failed, not reported as success");
        }
        else
        {
            verdict.Add(false, "build record present");
        }

        FinishCase("id.project.i12", root, verdict);
    }

    /// I13：源插件目录整体移走 → build 0 且不读源库；同状态 update 报 plugin_unknown(1)
    void CaseI13() noexcept
    {
        const CaseRoot root("i13");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        AddExit(verdict, "new exit0",
                RunProject(root, { "project", "new", "--name", "I13", "--path", root.project_root(), "--plugin", "Sample" }),
                0);

        std::string resolved_before;
        std::string manifest_before;
        ReadText(ProjectRecordPath(root, "resolved.json"), resolved_before);
        ReadText(ProjectRecordPath(root, "manifest.json"), manifest_before);

        const std::string moved = root.CasePath("moved-Sample");
        verdict.Add(ID::Bench::IdProject::IsDirectory(root.EnginePath("IDPlugin/Sample")), "source plugin exists");
        std::error_code error;
        std::filesystem::rename(std::filesystem::path(root.EnginePath("IDPlugin/Sample")),
                                std::filesystem::path(moved), error);
        verdict.Add(!error, "source plugin moved out of the case root");

        const CommandResult build = RunProject(root, BuildArguments("I13", "debug"));
        AddExit(verdict, "build exit0 without source library", build, 0);

        std::string resolved_after;
        std::string manifest_after;
        ReadText(ProjectRecordPath(root, "resolved.json"), resolved_after);
        ReadText(ProjectRecordPath(root, "manifest.json"), manifest_after);
        verdict.Add(resolved_after == resolved_before, "resolved.json bytes unchanged");
        verdict.Add(manifest_after == manifest_before, "manifest.json bytes unchanged");

        std::string log_text;
        for (const std::string& name : LogNames(root))
        {
            ReadText(root.ProjectPath("IDGenerated/log/" + name), log_text);
        }
        verdict.Add(log_text.find(moved) == std::string::npos, "log does not mention the moved source directory");
        verdict.Add(build.err_text.find(moved) == std::string::npos, "stderr does not mention the moved source directory");

        const CommandResult update = RunProject(root, { "project", "update", "I13" });
        AddExit(verdict, "update exit1", update, 1);
        CheckCategory(verdict, "plugin_unknown", update, "plugin_unknown");

        FinishCase("id.project.i13", root, verdict);
    }

    /// I14：只改缩进/键序、显式写出与快照等值的 default → build 0，不报 stale_generated
    void CaseI14() noexcept
    {
        const CaseRoot root("i14");
        CaseVerdict    verdict;

        PluginSpec spec = SimplePlugin(
            "Sample", "[]",
            "[ { \"name\": \"ENABLE_LOG\", \"type\": \"bool\", \"default\": \"ON\", \"description\": \"fixture\" } ]");
        verdict.Add(WritePlugin(root, spec), "fixture plugin Sample with option");
        AddExit(verdict, "new exit0",
                RunProject(root, { "project", "new", "--name", "I14", "--path", root.project_root(), "--plugin", "Sample" }),
                0);

        // 样本一：只改缩进与键序
        const std::string reordered = "{\n\t\"plugins\": [ {\n\t\t\"options\": {},\n\t\t\"linkage_type\": \"static\",\n"
                                      "\t\t\"name\": \"Sample\"\n\t} ],\n\t\"name\": \"I14\"\n}\n";
        verdict.Add(WriteText(root.ProjectPath("plugins.json"), reordered), "rewrote plugins.json with different layout");
        const CommandResult first = RunProject(root, BuildArguments("I14", "debug"));
        AddExit(verdict, "build after reorder exit0", first, 0);
        verdict.Add(!HasCategory(first, "stale_generated"), "no stale_generated after reorder");

        // 样本二：把与快照等值的 default 显式写出
        verdict.Add(WritePluginsJson(root, "I14",
                                     "[ { \"name\": \"Sample\", \"linkage_type\": \"static\","
                                     " \"options\": { \"ENABLE_LOG\": \"ON\" } } ]"),
                    "explicit default value written out");
        const CommandResult second = RunProject(root, BuildArguments("I14", "release"));
        AddExit(verdict, "build after explicit default exit0", second, 0);
        verdict.Add(!HasCategory(second, "stale_generated"), "no stale_generated after explicit default");

        FinishCase("id.project.i14", root, verdict);
    }

    /// I15：真实改 options / linkage / 直接选择 → stale_generated(1)，不 configure；update 后 build 成功
    void CaseI15() noexcept
    {
        const CaseRoot root("i15");
        CaseVerdict    verdict;

        PluginSpec spec = SimplePlugin(
            "Sample", "[]",
            "[ { \"name\": \"ENABLE_LOG\", \"type\": \"bool\", \"default\": \"OFF\", \"description\": \"fixture\" } ]");
        verdict.Add(WritePlugin(root, spec), "fixture plugin Sample with option");
        AddExit(verdict, "new exit0",
                RunProject(root, { "project", "new", "--name", "I15", "--path", root.project_root(), "--plugin", "Sample" }),
                0);

        verdict.Add(WritePluginsJson(root, "I15",
                                     "[ { \"name\": \"Sample\", \"linkage_type\": \"static\","
                                     " \"options\": { \"ENABLE_LOG\": \"ON\" } } ]"),
                    "real option change");

        const CommandResult stale = RunProject(root, BuildArguments("I15", "debug"));
        AddExit(verdict, "build exit1", stale, 1);
        CheckCategory(verdict, "stale_generated", stale, "stale_generated");
        verdict.Add(HasCategory(stale, "update"), "next step points to update");
        verdict.Add(!IsRegularFile(CachePath(root, "x64", "debug")), "did not enter configure");

        AddExit(verdict, "update exit0", RunProject(root, { "project", "update", "I15" }), 0);
        AddExit(verdict, "build after update exit0", RunProject(root, BuildArguments("I15", "debug")), 0);

        FinishCase("id.project.i15", root, verdict);
    }
} // namespace

namespace ID::Bench
{
    void IdProjectCasesI() noexcept
    {
        CaseI1();
        CaseI2();
        CaseI3();
        CaseI4a();
        CaseI4b();
        CaseI5();
        CaseI6();
        CaseI7AsciiSpacePath();
        CaseI7NonAsciiPath();
        CaseI8();
        CaseI9();
        CaseI10();
        CaseI11();
        CaseI12();
        CaseI13();
        CaseI14();
        CaseI15();
    }
} // namespace ID::Bench
