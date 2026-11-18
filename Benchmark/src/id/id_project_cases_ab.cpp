#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "benchmark_id.hpp"
#include "benchmark_support.hpp"
#include "id_project_cases.hpp"
#include "id_project_test_support.hpp"

/**
 *  `id.project` 正例（A1–A9）与幂等（B1–B5）用例。
 *
 *  A1 另带一条 `id.project.a1.limitations`：覆盖 spec §16 第 25 条（G8 限制的两个载体——
 *  `resolved.limitations` 与 `new` 成功摘要 / `new --check` / `build --check` 三处命令输出）。
 *
 *  真实构建组合按任务卡要求分派：A1 = 真实 `IDJson`（闭包带 `IDLog`）、A3 = 真实 `IDWindow`
 *  （带 `vendor/glfw-3.5.1` 共享 vendor）、A6 = `--empty`；其余需要真实构建的行用合成插件
 *  （`src/<Plugin>.cpp` 可编译），共用统一 profile `--generator "MinGW Makefiles" --toolchain mingw
 *  --arch x64`，产物断言落 `bin/mingw/x64/<config>/`。
 *
 *  每个用例只出一条判定行（`id.project.<case>`），子条件逐项记在 detail 里；失败时整棵
 *  `<caseRoot>` 归档到 `<benchmark>/build/id.project/<case>-fail/`，便于复查。
 */

namespace
{
    using ID::Bench::Check;
    using ID::Bench::IdProject::AppendMingwProfile;
    using ID::Bench::IdProject::ArtifactPath;
    using ID::Bench::IdProject::CaseRoot;
    using ID::Bench::IdProject::CaseVerdict;
    using ID::Bench::IdProject::CommandResult;
    using ID::Bench::IdProject::Contains;
    using ID::Bench::IdProject::CopyRealPlugin;
    using ID::Bench::IdProject::CopyRealSharedAssets;
    using ID::Bench::IdProject::FindRegistryEntry;
    using ID::Bench::IdProject::HasCategory;
    using ID::Bench::IdProject::IsDirectory;
    using ID::Bench::IdProject::IsRegularFile;
    using ID::Bench::IdProject::JsonReader;
    using ID::Bench::IdProject::ListTree;
    using ID::Bench::IdProject::PluginSpec;
    using ID::Bench::IdProject::ProjectRecordPath;
    using ID::Bench::IdProject::ReadBuildRecords;
    using ID::Bench::IdProject::ReadManifest;
    using ID::Bench::IdProject::ReadResolved;
    using ID::Bench::IdProject::ReadText;
    using ID::Bench::IdProject::RunProject;
    using ID::Bench::IdProject::Sha256Hex;
    using ID::Bench::IdProject::Sha256OfFile;
    using ID::Bench::IdProject::SpawnOptions;
    using ID::Bench::IdProject::SpawnProject;
    using ID::Bench::IdProject::SpawnResult;
    using ID::Bench::IdProject::TakeSnapshot;
    using ID::Bench::IdProject::SnapshotDiff;
    using ID::Bench::IdProject::WritePlugin;
    using ID::Bench::IdProject::WritePluginsJson;
    using ID::Bench::IdProject::WriteText;

    std::vector<std::string> MakeArgs(std::initializer_list<std::string_view> items)
    {
        std::vector<std::string> arguments;
        arguments.reserve(items.size());
        for (const std::string_view item : items)
        {
            arguments.emplace_back(item);
        }
        return arguments;
    }

    PluginSpec SimplePlugin(std::string name, std::string dependencies = "[]", std::string options = "[]")
    {
        PluginSpec spec;
        spec.name              = std::move(name);
        spec.dependencies_json = std::move(dependencies);
        spec.options_json      = std::move(options);
        return spec;
    }

    /// @brief 日志文件名形态 `^new-debug-<yyyyMMdd>T<HHmmss>Z-<4 位序号>\.log$` 且不含 ':'
    bool IsBuildLogName(std::string_view name) noexcept
    {
        if (name.size() != 35 || name.find(':') != std::string_view::npos)
        {
            return false;
        }
        if (name.substr(0, 10) != "new-debug-")
        {
            return false;
        }

        const auto is_digit = [](char unit) { return unit >= '0' && unit <= '9'; };
        for (std::size_t index = 10; index < 18; ++index)
        {
            if (!is_digit(name[index]))
            {
                return false;
            }
        }
        if (name[18] != 'T')
        {
            return false;
        }
        for (std::size_t index = 19; index < 25; ++index)
        {
            if (!is_digit(name[index]))
            {
                return false;
            }
        }
        if (name[25] != 'Z' || name[26] != '-')
        {
            return false;
        }
        for (std::size_t index = 27; index < 31; ++index)
        {
            if (!is_digit(name[index]))
            {
                return false;
            }
        }
        return name.substr(31) == ".log";
    }

    /// @brief 取 `IDGenerated/log/` 下普通文件的文件名（不含目录）
    std::vector<std::string> BuildLogNames(const CaseRoot& root) noexcept
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

    /// @brief `manifest.entries` 里是否存在指定 `(path, fragment)` 条目
    bool HasManifestEntry(const JsonReader& manifest, std::string_view path, std::string_view fragment) noexcept
    {
        const std::size_t count = manifest.Count("entries");
        for (std::size_t index = 0; index < count; ++index)
        {
            const std::string prefix = "entries." + std::to_string(index) + ".";
            if (manifest.String(prefix + "path") != path)
            {
                continue;
            }
            const ID::Json::JsonValue* node = manifest.Find(prefix + "fragment");
            const bool is_fragment_null      = node == nullptr || node->is_null();
            if (fragment.empty() && is_fragment_null)
            {
                return true;
            }
            if (!fragment.empty() && !is_fragment_null && node->is_string() && node->as_string() == fragment)
            {
                return true;
            }
        }
        return false;
    }

    /// @brief `manifest.entries` 里是否存在以某前缀开头的路径（裁剪断言用）
    bool HasManifestPathPrefix(const JsonReader& manifest, std::string_view prefix) noexcept
    {
        const std::size_t count = manifest.Count("entries");
        for (std::size_t index = 0; index < count; ++index)
        {
            if (manifest.String("entries." + std::to_string(index) + ".path").rfind(prefix, 0) == 0)
            {
                return true;
            }
        }
        return false;
    }

    /// @brief `resolved.selection` 是否含某插件，且回填 `is_direct`
    bool FindSelection(const JsonReader& resolved, std::string_view plugin, bool& is_direct) noexcept
    {
        const std::size_t count = resolved.Count("selection");
        for (std::size_t index = 0; index < count; ++index)
        {
            const std::string prefix = "selection." + std::to_string(index) + ".";
            if (resolved.String(prefix + "plugin") == plugin)
            {
                is_direct = resolved.Bool(prefix + "is_direct", false);
                return true;
            }
        }
        return false;
    }

    /// @brief `resolved.edges` 是否含 `from -> to` 边
    bool HasEdge(const JsonReader& resolved, std::string_view from, std::string_view to) noexcept
    {
        const std::size_t count = resolved.Count("edges");
        for (std::size_t index = 0; index < count; ++index)
        {
            const std::string prefix = "edges." + std::to_string(index) + ".";
            if (resolved.String(prefix + "from") == from && resolved.String(prefix + "to") == to)
            {
                return true;
            }
        }
        return false;
    }

    /// @brief 读 `build.json` 第 `index` 条记录的某个字段
    std::string BuildField(const JsonReader& records, std::size_t index, std::string_view field) noexcept
    {
        return records.String(std::to_string(index) + "." + std::string(field));
    }

    /// @brief 一份 fixture 常用的收尾：失败时归档整棵用例根
    void FinishCase(std::string_view name, const CaseRoot& root, const CaseVerdict& verdict) noexcept
    {
        Check(verdict.IsOk(), name, verdict.Detail());
        if (!verdict.IsOk())
        {
            root.ArchiveEvidence(std::string(name.substr(name.rfind('.') + 1)) + "-fail");
        }
    }

    // ==================== 正例 ====================

    /// A1：真实 `IDJson`（闭包带 `IDLog`）+ `--build`（夹具 profile，debug）
    void CaseA1() noexcept
    {
        const CaseRoot root("a1");
        CaseVerdict    verdict;

        verdict.Add(CopyRealPlugin(root, "IDJson") && CopyRealPlugin(root, "IDLog") && CopyRealSharedAssets(root),
                    "fixture(real IDJson/IDLog/Common)");

        std::vector<std::string> arguments =
            MakeArgs({"project", "new", "--name", "A1", "--path", root.project_root(), "--plugin", "IDJson", "--build"});
        AppendMingwProfile(arguments);
        arguments.emplace_back("--config");
        arguments.emplace_back("debug");

        const CommandResult result = RunProject(root, arguments);
        verdict.Add(result.exit_code == 0, "exit0");

        const std::string artifact = ArtifactPath(root, "mingw", "x64", "debug", "A1.exe");
        verdict.Add(IsRegularFile(artifact), "artifact bin/mingw/x64/debug/A1.exe");

        JsonReader resolved;
        JsonReader manifest;
        verdict.Add(ReadResolved(root, resolved), "resolved readable");
        verdict.Add(ReadManifest(root, manifest), "manifest readable");

        const std::string generation = resolved.String("generation_id");
        verdict.Add(!generation.empty() && generation == manifest.String("generation_id"), "generation match");

        bool              is_digest_ok = false;
        const std::string manifest_sha = Sha256OfFile(ProjectRecordPath(root, "manifest.json"), is_digest_ok);
        verdict.Add(is_digest_ok && resolved.String("manifest_digest") == manifest_sha,
                    "resolved.manifest_digest == sha256(manifest.json)");

        bool        is_initialized = false;
        std::size_t total          = 0;
        verdict.Add(FindRegistryEntry(root, "A1", is_initialized, total) && is_initialized,
                    "registry entry is_initialized=true");

        const std::vector<std::string> logs = BuildLogNames(root);
        bool                           has_log_name = false;
        for (const std::string& name : logs)
        {
            has_log_name = has_log_name || IsBuildLogName(name);
        }
        verdict.Add(logs.size() == 1 && has_log_name,
                    "log name unique and matches new-debug-<UTC>Z-<4>.log");

        FinishCase("id.project.a1", root, verdict);
    }

    /**
     *  A1 补充（spec §16 第 25 条、§14.1 G8）：模型缺口必须在**两个载体**上可观察，只在文档里写不算已告知：
     *  1. `resolved.limitations` 的首条 `code = g8_engine_option_not_projected` 且 `message` 非空；
     *  2. 命令输出：`new` 成功摘要、`new --check`、`build --check` 三处各打印该限制行。
     *
     *  取证口径：三处输出断言都只看 `out_text`（`print_limitations` 按 `Report.hpp` 第 4 条只写 stdout），
     *  不靠"实现源码里有这个字符串"代替命令输出。限制不是错误：它不改变退出码，`new --check` 仍按计划
     *  取 0 或 6；`build --check` 只复核将执行的命令，不真正 configure/build。
     */
    void CaseA1Limitations() noexcept
    {
        constexpr std::string_view kCode = "g8_engine_option_not_projected";

        const CaseRoot root("a1-limitations");
        CaseVerdict    verdict;

        // 载体二·1：new 成功摘要（`--empty` 是纯生成路径，不需要 profile，也不需要插件夹具）
        const CommandResult create =
            RunProject(root, {"project", "new", "--name", "A1Lim", "--path", root.project_root(), "--empty"});
        ID::Bench::IdProject::AddExit(verdict, "new exit0", create, 0);
        verdict.Add(create.out_text.find(kCode) != std::string::npos, "new success summary prints the limitation");

        // 载体一：resolved.limitations[0] 的 code 与 message
        JsonReader resolved;
        verdict.Add(ReadResolved(root, resolved), "resolved readable");
        verdict.Add(resolved.Count("limitations") >= 1, "resolved.limitations non-empty");
        verdict.Add(resolved.String("limitations.0.code") == kCode, "resolved.limitations[0].code");
        verdict.Add(!resolved.String("limitations.0.message").empty(), "resolved.limitations[0].message non-empty");

        // 载体二·2：new --check（只读，零写）
        const ID::Bench::IdProject::Snapshot before_new_check = TakeSnapshot(root);
        const CommandResult check_new =
            RunProject(root, {"project", "new", "--name", "A1Lim", "--path", root.project_root(), "--check"});
        const ID::Bench::IdProject::Snapshot after_new_check = TakeSnapshot(root);
        verdict.Add(check_new.exit_code == 0 || check_new.exit_code == 6, "new --check exit 0 or 6");
        verdict.Add(check_new.out_text.find(kCode) != std::string::npos, "new --check prints the limitation");
        verdict.Add(SnapshotDiff(before_new_check, after_new_check).empty(), "new --check zero write");

        // 载体二·3：build --check（只复核将执行的命令，不进入 configure/build）
        std::vector<std::string> arguments = MakeArgs({"project", "build", "A1Lim", "--check"});
        AppendMingwProfile(arguments);
        arguments.emplace_back("--config");
        arguments.emplace_back("debug");

        const ID::Bench::IdProject::Snapshot before_build_check = TakeSnapshot(root);
        const CommandResult check_build                         = RunProject(root, arguments);
        const ID::Bench::IdProject::Snapshot after_build_check  = TakeSnapshot(root);
        ID::Bench::IdProject::AddExit(verdict, "build --check exit0", check_build, 0);
        verdict.Add(check_build.out_text.find(kCode) != std::string::npos, "build --check prints the limitation");
        verdict.Add(SnapshotDiff(before_build_check, after_build_check).empty(), "build --check zero write");

        FinishCase("id.project.a1.limitations", root, verdict);
    }

    /// A2：已提交项目上 `new --check` 不执行，快照完全一致
    void CaseA2() noexcept
    {
        const CaseRoot root("a2");
        CaseVerdict    verdict;

        const CommandResult baseline =
            RunProject(root, {"project", "new", "--name", "A2", "--path", root.project_root(), "--empty"});
        verdict.Add(baseline.exit_code == 0, "baseline new exit0");

        const ID::Bench::IdProject::Snapshot before = TakeSnapshot(root);
        const CommandResult check = RunProject(root, {"project", "new", "--name", "A2", "--path", root.project_root(), "--check"});
        const ID::Bench::IdProject::Snapshot after = TakeSnapshot(root);

        verdict.Add(check.exit_code == 0 || check.exit_code == 6, "check exit 0 or 6");
        const std::string diff = SnapshotDiff(before, after);
        verdict.Add(diff.empty(), "snapshot identical");
        if (!diff.empty())
        {
            verdict.Note("diff", diff.substr(0, 400));
        }
        verdict.Add(!IsRegularFile(ProjectRecordPath(root, "intent.json")), "no intent.json");
        FinishCase("id.project.a2", root, verdict);
    }

    /// A3：真实 `IDWindow`（含 vendor/glfw 共享 vendor）先 `new`（不构建）再 `build`
    void CaseA3() noexcept
    {
        const CaseRoot root("a3");
        CaseVerdict    verdict;

        verdict.Add(CopyRealPlugin(root, "IDWindow") && CopyRealPlugin(root, "IDLog") && CopyRealSharedAssets(root),
                    "fixture(real IDWindow/IDLog/Common/Vendor)");

        const CommandResult create =
            RunProject(root, {"project", "new", "--name", "A3", "--path", root.project_root(), "--plugin", "IDWindow"});
        verdict.Add(create.exit_code == 0, "new exit0");

        bool        is_initialized = false;
        std::size_t total          = 0;
        verdict.Add(FindRegistryEntry(root, "A3", is_initialized, total) && is_initialized,
                    "is_initialized=true after new");
        verdict.Add(!IsDirectory(root.ProjectPath("build")), "no build/ after new");
        verdict.Add(!IsDirectory(root.ProjectPath("bin")), "no bin/ after new");
        verdict.Add(!IsRegularFile(ProjectRecordPath(root, "build.json")), "no build.json after new");

        std::vector<std::string> arguments = MakeArgs({"project", "build", "A3"});
        AppendMingwProfile(arguments);
        arguments.emplace_back("--config");
        arguments.emplace_back("debug");

        const CommandResult build = RunProject(root, arguments);
        verdict.Add(build.exit_code == 0, "build exit0");

        JsonReader records;
        verdict.Add(ReadBuildRecords(root, records) && records.Count("") == 1, "exactly one build record");
        verdict.Add(BuildField(records, 0, "verdict") == "success", "record verdict=success");
        verdict.Add(IsRegularFile(ArtifactPath(root, "mingw", "x64", "debug", "A3.exe")),
                    "artifact bin/mingw/x64/debug/A3.exe");

        FinishCase("id.project.a3", root, verdict);
    }

    /// A4：直接选择由 A 换成 B → `update` 产生新 generation 并裁剪 A 的副本 → `build`
    void CaseA4() noexcept
    {
        const CaseRoot root("a4");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("A")), "fixture plugin A");
        verdict.Add(WritePlugin(root, SimplePlugin("B")), "fixture plugin B");

        const CommandResult create =
            RunProject(root, {"project", "new", "--name", "A4", "--path", root.project_root(), "--plugin", "A"});
        verdict.Add(create.exit_code == 0, "new exit0");

        JsonReader resolved_before;
        verdict.Add(ReadResolved(root, resolved_before), "resolved readable");
        const std::string generation_before = resolved_before.String("generation_id");

        verdict.Add(WritePluginsJson(root, "A4",
                                     "[ { \"name\": \"B\", \"linkage_type\": \"static\", \"options\": {} } ]"),
                    "user edited plugins.json to B");

        const CommandResult update = RunProject(root, {"project", "update", "A4"});
        verdict.Add(update.exit_code == 0, "update exit0");

        JsonReader resolved_after;
        verdict.Add(ReadResolved(root, resolved_after), "resolved readable after update");
        verdict.Add(!generation_before.empty() && resolved_after.String("generation_id") != generation_before,
                    "new generation");

        verdict.Add(!IsDirectory(root.ProjectPath("IDPlugin/A")), "IDPlugin/A pruned");

        JsonReader manifest;
        verdict.Add(ReadManifest(root, manifest), "manifest readable");
        verdict.Add(!HasManifestPathPrefix(manifest, "IDPlugin/A/"), "manifest has no IDPlugin/A/** entry");
        verdict.Add(IsDirectory(root.ProjectPath("IDPlugin/B")), "IDPlugin/B exists");

        bool is_direct = false;
        verdict.Add(FindSelection(resolved_after, "B", is_direct) && is_direct, "selection contains B with is_direct=true");

        bool        is_initialized = false;
        std::size_t total          = 0;
        verdict.Add(FindRegistryEntry(root, "A4", is_initialized, total) && is_initialized, "is_initialized stays true");
        verdict.Add(!IsRegularFile(ProjectRecordPath(root, "intent.json")), "no intent.json");

        std::vector<std::string> arguments = MakeArgs({"project", "build", "A4"});
        AppendMingwProfile(arguments);
        arguments.emplace_back("--config");
        arguments.emplace_back("debug");
        const CommandResult build = RunProject(root, arguments);
        verdict.Add(build.exit_code == 0, "build exit0");
        verdict.Add(IsRegularFile(ArtifactPath(root, "mingw", "x64", "debug", "A4.exe")), "artifact exists");

        FinishCase("id.project.a4", root, verdict);
    }

    /// A5：删掉仍被下游依赖的上游直接选择 → 闭包补回，用户文件不改写
    void CaseA5() noexcept
    {
        const CaseRoot root("a5");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Up")), "fixture plugin Up");
        verdict.Add(WritePlugin(root, SimplePlugin("Down", "[ \"Up\" ]")), "fixture plugin Down(dep Up)");

        const CommandResult create =
            RunProject(root, { "project", "new", "--name", "A5", "--path", root.project_root(), "--plugin", "Up",
                               "--plugin", "Down" });
        ID::Bench::IdProject::AddExit(verdict, "new exit0", create, 0);

        verdict.Add(WritePluginsJson(root, "A5",
                                     "[ { \"name\": \"Down\", \"linkage_type\": \"static\", \"options\": {} } ]"),
                    "user removed Up from direct selection");

        std::string plugins_before;
        verdict.Add(ReadText(root.ProjectPath("plugins.json"), plugins_before), "plugins.json readable");
        const std::string plugins_digest_before = Sha256Hex(plugins_before);

        // 闭包补齐说明由模块日志器写进程 stderr（不经 `CommandIo`），进程内驱动看不到它；
        // 因此这一步走子进程，子进程的两路输出被重定向到文件后判定（spec §10.1 要求以 info 说明）
        ID::Bench::IdProject::SpawnOptions options;
        options.timeout_ms = 120000;
        const ID::Bench::IdProject::SpawnResult update =
            ID::Bench::IdProject::SpawnProject(root, "a5-update", { "project", "update", "A5" }, options);
        verdict.Add(update.is_spawned && !update.is_timed_out && update.exit_code == 0, "update exit0 (subprocess)");

        JsonReader resolved;
        verdict.Add(ReadResolved(root, resolved), "resolved readable");

        bool is_direct = true;
        verdict.Add(FindSelection(resolved, "Up", is_direct) && !is_direct, "Up closed back with is_direct=false");
        verdict.Add(HasEdge(resolved, "Down", "Up"), "edge Down -> Up");
        verdict.Add(Contains(update, "Up") && Contains(update, "Down"), "closure info names both plugins");
        verdict.Add(Contains(update, "补齐"), "closure completion explained as info (spec §10.1)");

        std::string plugins_after;
        verdict.Add(ReadText(root.ProjectPath("plugins.json"), plugins_after)
                        && Sha256Hex(plugins_after) == plugins_digest_before,
                    "plugins.json bytes unchanged");

        FinishCase("id.project.a5", root, verdict);
    }

    /// A6：`--empty` + `build`（真实构建组合之一），无插件链接
    void CaseA6() noexcept
    {
        const CaseRoot root("a6");
        CaseVerdict    verdict;

        const CommandResult create =
            RunProject(root, {"project", "new", "--name", "A6", "--path", root.project_root(), "--empty"});
        ID::Bench::IdProject::AddExit(verdict, "new --empty exit0", create, 0);

        std::string plugins_json;
        verdict.Add(ReadText(root.ProjectPath("plugins.json"), plugins_json)
                        && plugins_json.find("\"plugins\": []") != std::string::npos,
                    "plugins.json is an empty array");

        JsonReader resolved;
        verdict.Add(ReadResolved(root, resolved), "resolved readable");
        verdict.Add(resolved.Count("selection") == 0, "resolved.selection empty");

        std::string root_cmake;
        verdict.Add(ReadText(root.ProjectPath("CMakeLists.txt"), root_cmake), "root CMakeLists readable");
        verdict.Add(root_cmake.find("target_link_libraries") == std::string::npos,
                    "root CMakeLists has no target_link_libraries");

        std::vector<std::string> arguments = MakeArgs({"project", "build", "A6"});
        AppendMingwProfile(arguments);
        arguments.emplace_back("--config");
        arguments.emplace_back("debug");
        const CommandResult build = RunProject(root, arguments);
        verdict.Add(build.exit_code == 0, "build exit0");
        verdict.Add(IsRegularFile(ArtifactPath(root, "mingw", "x64", "debug", "A6.exe")),
                    "artifact bin/mingw/x64/debug/A6.exe");

        FinishCase("id.project.a6", root, verdict);
    }

    /// A7：非交互且没有选择来源 → `cli_usage`（3），不等待输入，诊断给可复制形式
    void CaseA7() noexcept
    {
        const CaseRoot root("a7");
        CaseVerdict    verdict;

        SpawnOptions options;
        options.is_stdin_null = true;
        options.timeout_ms    = 10000;

        const std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
        const SpawnResult result = SpawnProject(root, "a7", {"project", "new", "--name", "A7", "--path", root.project_root()},
                                               options);
        const long long elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::steady_clock::now() - begin)
                                         .count();

        verdict.Add(result.is_spawned && !result.is_timed_out, "spawned without timeout");
        verdict.Add(result.exit_code == 3, "exit 3 (cli_usage)");
        verdict.Add(HasCategory(result, "cli_usage"), "diagnostic category cli_usage");
        verdict.Add(result.err_text.find("--plugin") != std::string::npos
                        && result.err_text.find("--empty") != std::string::npos,
                    "stderr shows copyable --plugin and --empty forms");
        verdict.Add(elapsed_ms < 5000, "exits within 5 s without waiting for input");

        FinishCase("id.project.a7", root, verdict);
    }

    /// A8：同一项目三种 profile 构建，产物与 cache 互不覆盖，generation 不变
    void CaseA8() noexcept
    {
        const CaseRoot root("a8");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        const CommandResult create =
            RunProject(root, {"project", "new", "--name", "A8", "--path", root.project_root(), "--plugin", "Sample"});
        verdict.Add(create.exit_code == 0, "new exit0");

        std::vector<std::string> debug_args = MakeArgs({"project", "build", "A8"});
        AppendMingwProfile(debug_args);
        debug_args.emplace_back("--config");
        debug_args.emplace_back("debug");
        const CommandResult debug_build = RunProject(root, debug_args);

        std::vector<std::string> release_args = MakeArgs({"project", "build", "A8"});
        AppendMingwProfile(release_args);
        release_args.emplace_back("--config");
        release_args.emplace_back("release");
        const CommandResult release_build = RunProject(root, release_args);

        std::vector<std::string> host_args = MakeArgs({"project", "build", "A8"});
        host_args.emplace_back("--generator");
        host_args.emplace_back("MinGW Makefiles");
        host_args.emplace_back("--toolchain");
        host_args.emplace_back("mingw");
        host_args.emplace_back("--arch");
        host_args.emplace_back("host");
        host_args.emplace_back("--config");
        host_args.emplace_back("debug");
        const CommandResult host_build = RunProject(root, host_args);

        verdict.Add(debug_build.exit_code == 0, "build debug exit0");
        verdict.Add(release_build.exit_code == 0, "build release exit0");
        verdict.Add(host_build.exit_code == 0, "build --arch host exit0");

        verdict.Add(IsRegularFile(ArtifactPath(root, "mingw", "x64", "debug", "A8.exe")), "artifact mingw/x64/debug");
        verdict.Add(IsRegularFile(ArtifactPath(root, "mingw", "x64", "release", "A8.exe")), "artifact mingw/x64/release");
        verdict.Add(IsRegularFile(ArtifactPath(root, "mingw", "host", "debug", "A8.exe")), "artifact mingw/host/debug");

        JsonReader records;
        verdict.Add(ReadBuildRecords(root, records) && records.Count("") == 3, "three build records");

        const std::string generation = BuildField(records, 0, "generation_id");
        const std::string digest     = BuildField(records, 0, "manifest_digest");
        bool              is_same    = !generation.empty() && !digest.empty();
        for (std::size_t index = 1; index < 3; ++index)
        {
            is_same = is_same && BuildField(records, index, "generation_id") == generation
                   && BuildField(records, index, "manifest_digest") == digest;
        }
        verdict.Add(is_same, "all records share generation_id and manifest_digest");

        FinishCase("id.project.a8", root, verdict);
    }

    /// A9：纯生成行不需要 profile；环境无默认生成器也不报 build_environment_unsupported
    void CaseA9() noexcept
    {
        const CaseRoot root("a9");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");

        // PATH 只保留工具链 bin（ID.exe 的运行期 DLL 来自这里或可执行文件所在目录）与系统目录：
        // 目的是"环境里没有可用的 cmake"，不是伪造一个残废环境
        const std::string compiler_dir =
            std::filesystem::path(std::string(BENCHMARK_CXX_COMPILER)).parent_path().string();

        SpawnOptions options;
        options.is_path_overridden      = true;
        options.path_value              = compiler_dir + ";" + std::string("C:\\Windows\\system32;C:\\Windows");
        options.is_generator_overridden = true;
        options.generator_value         = std::string();
        options.timeout_ms              = 60000;

        const SpawnResult result =
            SpawnProject(root, "a9", {"project", "new", "--name", "A9", "--path", root.project_root(), "--plugin", "Sample"},
                         options);

        verdict.Add(result.is_spawned && !result.is_timed_out, "spawned");
        ID::Bench::IdProject::AddExit(verdict, "exit 0 without profile", result, 0);
        verdict.Add(!HasCategory(result, "build_environment_unsupported"), "no build_environment_unsupported");
        verdict.Add(!IsDirectory(root.ProjectPath("build")), "no build/");
        verdict.Add(!IsDirectory(root.ProjectPath("bin")), "no bin/");

        bool        is_initialized = false;
        std::size_t total          = 0;
        verdict.Add(FindRegistryEntry(root, "A9", is_initialized, total) && is_initialized, "is_initialized=true");

        FinishCase("id.project.a9", root, verdict);
    }

    // ==================== 幂等 ====================

    /// B1：已提交项目重复 `new`（无选择参数）为 no-op；带选择参数报 selection_source_conflict
    void CaseB1() noexcept
    {
        const CaseRoot root("b1");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        const CommandResult create =
            RunProject(root, {"project", "new", "--name", "B1", "--path", root.project_root(), "--plugin", "Sample"});
        verdict.Add(create.exit_code == 0, "initial new exit0");

        std::string registry_before;
        std::string plugins_before;
        verdict.Add(ReadText(ID::Bench::IdProject::RegistryPath(root), registry_before), "registry readable");
        verdict.Add(ReadText(root.ProjectPath("plugins.json"), plugins_before), "plugins.json readable");

        JsonReader resolved_before;
        verdict.Add(ReadResolved(root, resolved_before), "resolved readable");
        const std::string generation_before = resolved_before.String("generation_id");

        const ID::Bench::IdProject::Snapshot snapshot_before = TakeSnapshot(root);
        const CommandResult repeat = RunProject(root, {"project", "new", "--name", "B1", "--path", root.project_root()});
        const ID::Bench::IdProject::Snapshot snapshot_after = TakeSnapshot(root);

        verdict.Add(repeat.exit_code == 0, "repeat new exit0");
        verdict.Add(SnapshotDiff(snapshot_before, snapshot_after).empty(), "no-op: snapshot identical");

        std::string registry_after;
        std::string plugins_after;
        verdict.Add(ReadText(ID::Bench::IdProject::RegistryPath(root), registry_after) && registry_after == registry_before,
                    "registry bytes unchanged");
        verdict.Add(ReadText(root.ProjectPath("plugins.json"), plugins_after) && plugins_after == plugins_before,
                    "plugins.json bytes unchanged");

        JsonReader resolved_after;
        verdict.Add(ReadResolved(root, resolved_after) && resolved_after.String("generation_id") == generation_before,
                    "generation unchanged");
        verdict.Add(!IsRegularFile(ProjectRecordPath(root, "intent.json")), "no intent.json");

        // 负例子项：带 --plugin 重跑
        const CommandResult conflict =
            RunProject(root, {"project", "new", "--name", "B1", "--path", root.project_root(), "--plugin", "Sample"});
        verdict.Add(conflict.exit_code == 3 && HasCategory(conflict, "selection_source_conflict"),
                    "re-run with --plugin -> selection_source_conflict(3)");

        FinishCase("id.project.b1", root, verdict);
    }

    /// B1b：改 `plugins.json` 后跑 `new` → stale_generated（1），记录与用户文件都不变
    void CaseB1b() noexcept
    {
        const CaseRoot root("b1b");
        CaseVerdict    verdict;

        PluginSpec spec = SimplePlugin(
            "Sample", "[]",
            "[ { \"name\": \"ENABLE_LOG\", \"type\": \"bool\", \"default\": \"OFF\", \"description\": \"fixture option\" } ]");
        verdict.Add(WritePlugin(root, spec), "fixture plugin Sample with option");

        const CommandResult create =
            RunProject(root, {"project", "new", "--name", "B1b", "--path", root.project_root(), "--plugin", "Sample"});
        verdict.Add(create.exit_code == 0, "initial new exit0");

        std::string resolved_before;
        std::string manifest_before;
        verdict.Add(ReadText(ProjectRecordPath(root, "resolved.json"), resolved_before), "resolved readable");
        verdict.Add(ReadText(ProjectRecordPath(root, "manifest.json"), manifest_before), "manifest readable");

        verdict.Add(WritePluginsJson(root, "B1b",
                                     "[ { \"name\": \"Sample\", \"linkage_type\": \"static\","
                                     " \"options\": { \"ENABLE_LOG\": \"ON\" } } ]"),
                    "user changed option value");
        std::string plugins_before;
        verdict.Add(ReadText(root.ProjectPath("plugins.json"), plugins_before), "plugins.json readable");

        const CommandResult repeat = RunProject(root, {"project", "new", "--name", "B1b", "--path", root.project_root()});
        verdict.Add(repeat.exit_code == 1 && HasCategory(repeat, "stale_generated"),
                    "new after change -> stale_generated(1)");
        verdict.Add(Contains(repeat, "update"), "diagnostic points to update");

        std::string resolved_after;
        std::string manifest_after;
        std::string plugins_after;
        verdict.Add(ReadText(ProjectRecordPath(root, "resolved.json"), resolved_after) && resolved_after == resolved_before,
                    "resolved bytes unchanged");
        verdict.Add(ReadText(ProjectRecordPath(root, "manifest.json"), manifest_after) && manifest_after == manifest_before,
                    "manifest bytes unchanged");
        verdict.Add(ReadText(root.ProjectPath("plugins.json"), plugins_after) && plugins_after == plugins_before,
                    "plugins.json bytes unchanged");

        bool        is_initialized = false;
        std::size_t total          = 0;
        verdict.Add(FindRegistryEntry(root, "B1b", is_initialized, total) && is_initialized, "is_initialized stays true");

        FinishCase("id.project.b1b", root, verdict);
    }

    /// B2：同输入两次 `update`，第二次是 no-op（记录字节与 mtime 都不变）
    void CaseB2() noexcept
    {
        const CaseRoot root("b2");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        const CommandResult create =
            RunProject(root, {"project", "new", "--name", "B2", "--path", root.project_root(), "--plugin", "Sample"});
        verdict.Add(create.exit_code == 0, "new exit0");

        const CommandResult first = RunProject(root, {"project", "update", "B2"});
        verdict.Add(first.exit_code == 0, "first update exit0");

        std::string resolved_before;
        std::string manifest_before;
        verdict.Add(ReadText(ProjectRecordPath(root, "resolved.json"), resolved_before), "resolved readable");
        verdict.Add(ReadText(ProjectRecordPath(root, "manifest.json"), manifest_before), "manifest readable");

        const ID::Bench::IdProject::Snapshot snapshot_before = TakeSnapshot(root);
        const CommandResult second = RunProject(root, {"project", "update", "B2"});
        const ID::Bench::IdProject::Snapshot snapshot_after = TakeSnapshot(root);

        verdict.Add(second.exit_code == 0, "second update exit0");
        const std::string diff = SnapshotDiff(snapshot_before, snapshot_after);
        verdict.Add(diff.empty(), "second update is a no-op (bytes and mtime unchanged)");
        if (!diff.empty())
        {
            verdict.Note("diff", diff.substr(0, 400));
        }
        verdict.Add(!IsRegularFile(ProjectRecordPath(root, "intent.json")), "no intent.json");

        FinishCase("id.project.b2", root, verdict);
    }

    /// B3：同配置两次 `build`，两条 success 记录，不误报 artifact_missing
    void CaseB3() noexcept
    {
        const CaseRoot root("b3");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        const CommandResult create =
            RunProject(root, {"project", "new", "--name", "B3", "--path", root.project_root(), "--plugin", "Sample"});
        verdict.Add(create.exit_code == 0, "new exit0");

        std::vector<std::string> arguments = MakeArgs({"project", "build", "B3"});
        AppendMingwProfile(arguments);
        arguments.emplace_back("--config");
        arguments.emplace_back("debug");

        const CommandResult first  = RunProject(root, arguments);
        const CommandResult second = RunProject(root, arguments);

        verdict.Add(first.exit_code == 0, "first build exit0");
        verdict.Add(second.exit_code == 0, "second build exit0");

        JsonReader records;
        verdict.Add(ReadBuildRecords(root, records) && records.Count("") == 2, "two build records");
        verdict.Add(BuildField(records, 0, "verdict") == "success" && BuildField(records, 1, "verdict") == "success",
                    "both verdict=success");
        verdict.Add(BuildField(records, 0, "generation_id") == BuildField(records, 1, "generation_id")
                        && BuildField(records, 0, "manifest_digest") == BuildField(records, 1, "manifest_digest"),
                    "same generation_id and manifest_digest");
        verdict.Add(!HasCategory(second, "artifact_missing"), "second build does not report artifact_missing");

        FinishCase("id.project.b3", root, verdict);
    }

    /// B4：首次 `new` 写出缺省初值后 `update` 为 no-op，紧随的 build 不报 stale_generated
    void CaseB4() noexcept
    {
        const CaseRoot root("b4");
        CaseVerdict    verdict;

        PluginSpec spec = SimplePlugin(
            "Sample", "[]",
            "[ { \"name\": \"ENABLE_LOG\", \"type\": \"bool\", \"default\": \"ON\", \"description\": \"fixture option\" } ]");
        verdict.Add(WritePlugin(root, spec), "fixture plugin Sample with defaulted option");

        const CommandResult create =
            RunProject(root, {"project", "new", "--name", "B4", "--path", root.project_root(), "--plugin", "Sample"});
        verdict.Add(create.exit_code == 0, "new exit0");

        JsonReader resolved_before;
        JsonReader manifest_before;
        verdict.Add(ReadResolved(root, resolved_before), "resolved readable");
        verdict.Add(ReadManifest(root, manifest_before), "manifest readable");

        const ID::Bench::IdProject::Snapshot snapshot_before = TakeSnapshot(root);
        const CommandResult update = RunProject(root, {"project", "update", "B4"});
        const ID::Bench::IdProject::Snapshot snapshot_after = TakeSnapshot(root);

        ID::Bench::IdProject::AddExit(verdict, "update exit0", update, 0);
        const std::string update_diff = SnapshotDiff(snapshot_before, snapshot_after);
        verdict.Add(update_diff.empty(), "update is a no-op");
        if (!update_diff.empty())
        {
            verdict.Note("diff", update_diff.substr(0, 400));
        }
        verdict.Add(!IsRegularFile(ProjectRecordPath(root, "intent.json")), "no intent.json");

        JsonReader resolved_after;
        JsonReader manifest_after;
        verdict.Add(ReadResolved(root, resolved_after)
                        && resolved_after.String("generation_id") == resolved_before.String("generation_id"),
                    "generation unchanged");
        verdict.Add(ReadManifest(root, manifest_after)
                        && manifest_after.String("entries_digest") == manifest_before.String("entries_digest"),
                    "manifest digest unchanged");

        std::vector<std::string> arguments = MakeArgs({"project", "build", "B4"});
        AppendMingwProfile(arguments);
        arguments.emplace_back("--config");
        arguments.emplace_back("debug");
        const CommandResult build = RunProject(root, arguments);

        verdict.Add(build.exit_code == 0, "build exit0");
        verdict.Add(!HasCategory(build, "stale_generated"), "build does not report stale_generated");

        FinishCase("id.project.b4", root, verdict);
    }

    /// B5：身份稳定性三步（显式写出等值 default / 换选择触发裁剪 / 无变化再 update）
    void CaseB5() noexcept
    {
        const CaseRoot root("b5");
        CaseVerdict    verdict;

        PluginSpec sample = SimplePlugin(
            "Sample", "[]",
            "[ { \"name\": \"ENABLE_LOG\", \"type\": \"bool\", \"default\": \"ON\", \"description\": \"fixture option\" } ]");
        verdict.Add(WritePlugin(root, sample), "fixture plugin Sample");
        verdict.Add(WritePlugin(root, SimplePlugin("B")), "fixture plugin B");

        const CommandResult create =
            RunProject(root, {"project", "new", "--name", "B5", "--path", root.project_root(), "--plugin", "Sample"});
        verdict.Add(create.exit_code == 0, "new exit0");

        JsonReader resolved_v0;
        verdict.Add(ReadResolved(root, resolved_v0), "resolved readable");
        const std::string generation_v0 = resolved_v0.String("generation_id");

        // ① 把 default 显式写出（与快照等值）→ update 0 且 generation 不变
        verdict.Add(WritePluginsJson(root, "B5",
                                     "[ { \"name\": \"Sample\", \"linkage_type\": \"static\","
                                     " \"options\": { \"ENABLE_LOG\": \"ON\" } } ]"),
                    "① explicit default value written out");
        const CommandResult step1 = RunProject(root, {"project", "update", "B5"});
        JsonReader        resolved_v1;
        verdict.Add(ReadResolved(root, resolved_v1), "resolved readable after ①");
        verdict.Add(step1.exit_code == 0, "① update exit0");
        verdict.Add(resolved_v1.String("generation_id") == generation_v0, "① generation unchanged");
        verdict.Add(!IsRegularFile(ProjectRecordPath(root, "intent.json")), "① no intent");

        // ② 直接选择由 Sample 换成 B（触发裁剪）→ 新 generation，Sample 副本被裁剪
        verdict.Add(WritePluginsJson(root, "B5",
                                     "[ { \"name\": \"B\", \"linkage_type\": \"static\", \"options\": {} } ]"),
                    "② direct selection Sample -> B");
        const CommandResult step2 = RunProject(root, {"project", "update", "B5"});
        JsonReader        resolved_v2;
        verdict.Add(ReadResolved(root, resolved_v2), "resolved readable after ②");
        const std::string generation_v2 = resolved_v2.String("generation_id");
        verdict.Add(step2.exit_code == 0, "② update exit0");
        verdict.Add(!generation_v2.empty() && generation_v2 != generation_v0, "② new generation");
        verdict.Add(!IsDirectory(root.ProjectPath("IDPlugin/Sample")), "② IDPlugin/Sample pruned");

        // ③ 不改输入再 update → no-op，generation 仍是 ② 的取值
        JsonReader resolved_before3;
        verdict.Add(ReadResolved(root, resolved_before3), "resolved readable before ③");
        const ID::Bench::IdProject::Snapshot snapshot_before3 = TakeSnapshot(root);
        const CommandResult step3 = RunProject(root, {"project", "update", "B5"});
        const ID::Bench::IdProject::Snapshot snapshot_after3 = TakeSnapshot(root);
        JsonReader resolved_v3;
        verdict.Add(ReadResolved(root, resolved_v3), "resolved readable after ③");
        verdict.Add(step3.exit_code == 0, "③ update exit0");
        verdict.Add(SnapshotDiff(snapshot_before3, snapshot_after3).empty(), "③ no-op");
        verdict.Add(resolved_v3.String("generation_id") == generation_v2, "③ generation stays at ② value");
        verdict.Add(!IsRegularFile(ProjectRecordPath(root, "intent.json")), "③ no intent");

        FinishCase("id.project.b5", root, verdict);
    }
} // namespace

namespace ID::Bench
{
    void IdProjectCasesAB() noexcept
    {
        CaseA1();
        CaseA1Limitations();
        CaseA2();
        CaseA3();
        CaseA4();
        CaseA5();
        CaseA6();
        CaseA7();
        CaseA8();
        CaseA9();
        CaseB1();
        CaseB1b();
        CaseB2();
        CaseB3();
        CaseB4();
        CaseB5();
    }
} // namespace ID::Bench
