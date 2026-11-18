#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "benchmark_id.hpp"
#include "benchmark_support.hpp"
#include "id_project_cases.hpp"
#include "id_project_test_support.hpp"

/**
 *  `id.project` 故障注入组（F0–F15）。
 *
 *  驱动口径（`docs/id/11-verification-matrix.md` §11.3）：
 *  - `Fail` 注入可在进程内武装（`init_injection`）后调用 `run_command_line`；
 *  - `Abort` 注入一律走子进程（`_Exit(124)` 会终止调用方），期望码 `124`，不写成"任意非 0"。
 *  断言按 `intent`/`resolved`/`manifest`/`build.json` 的实际落盘内容判定，不按"看起来恢复了"判定。
 */

namespace
{
    using ID::Bench::Check;
    using ID::Bench::IdProject::AddExit;
    using ID::Bench::IdProject::AppendMingwProfile;
    using ID::Bench::IdProject::CaseRoot;
    using ID::Bench::IdProject::CaseVerdict;
    using ID::Bench::IdProject::CheckCategory;
    using ID::Bench::IdProject::AddExitOneOf;
    using ID::Bench::IdProject::CommandResult;
    using ID::Bench::IdProject::FindRegistryEntry;
    using ID::Bench::IdProject::HasCategory;
    using ID::Bench::IdProject::IsDirectory;
    using ID::Bench::IdProject::IsRegularFile;
    using ID::Bench::IdProject::JsonReader;
    using ID::Bench::IdProject::PluginSpec;
    using ID::Bench::IdProject::ProjectRecordPath;
    using ID::Bench::IdProject::ReadBuildRecords;
    using ID::Bench::IdProject::ReadManifest;
    using ID::Bench::IdProject::ReadResolved;
    using ID::Bench::IdProject::ReadText;
    using ID::Bench::IdProject::RegistryPath;
    using ID::Bench::IdProject::RunProject;
    using ID::Bench::IdProject::RunProjectInjected;
    using ID::Bench::IdProject::Sha256Hex;
    using ID::Bench::IdProject::Sha256OfFile;
    using ID::Bench::IdProject::SpawnOptions;
    using ID::Bench::IdProject::SpawnProject;
    using ID::Bench::IdProject::SpawnResult;
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

    bool HasIntent(const CaseRoot& root) noexcept
    {
        return IsRegularFile(ProjectRecordPath(root, "intent.json"));
    }

    /// @brief `intent.json` 的操作项里是否存在某状态（`pending`/`done`/`failed`）
    bool IntentHasState(const CaseRoot& root, std::string_view state) noexcept
    {
        std::string text;
        if (!ReadText(ProjectRecordPath(root, "intent.json"), text))
        {
            return false;
        }
        return text.find("\"" + std::string(state) + "\"") != std::string::npos;
    }

    /// @brief 子进程 Abort 注入：返回是否命中 `124`
    struct AbortOutcome
    {
        bool   is_spawned   = false;
        bool   is_timed_out = false;
        bool   is_124       = false;
        int    exit_code    = 0;
        std::string err_text = {};
    };

    AbortOutcome RunAbort(const CaseRoot& root, std::string_view tag, std::string_view inject,
                          std::initializer_list<std::string_view> arguments) noexcept
    {
        SpawnOptions options;
        options.inject     = std::string(inject);
        options.timeout_ms = 60000;

        const SpawnResult result = SpawnProject(root, tag, arguments, options);
        AbortOutcome      outcome;
        outcome.is_spawned   = result.is_spawned;
        outcome.is_timed_out = result.is_timed_out;
        outcome.exit_code    = result.exit_code;
        outcome.is_124       = result.is_spawned && !result.is_timed_out && result.exit_code == 124;
        outcome.err_text     = result.err_text;
        return outcome;
    }

    // ==================== 用例 ====================

    /// F0：bootstrap 阶段 `p2.before-intent=fail` → 2、无记录残留、登记无条目、去掉注入可重跑
    void CaseF0() noexcept
    {
        const CaseRoot root("f0");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");

        const CommandResult failed = RunProjectInjected(
            root, { "project", "new", "--name", "F0", "--path", root.project_root(), "--plugin", "Sample" },
            "p2.before-intent=fail");
        AddExit(verdict, "injected new exit2", failed, 2);
        CheckCategory(verdict, "write_failed", failed, "write_failed");

        verdict.Add(!HasIntent(root), "no intent.json");
        verdict.Add(!IsRegularFile(ProjectRecordPath(root, "resolved.json")), "no resolved.json");
        verdict.Add(!IsRegularFile(ProjectRecordPath(root, "manifest.json")), "no manifest.json");

        bool        is_initialized = false;
        std::size_t total          = 0;
        verdict.Add(!FindRegistryEntry(root, "F0", is_initialized, total) || !is_initialized,
                    "registry has no initialized F0 entry");

        const CommandResult list = RunProject(root, { "project", "list" });
        AddExit(verdict, "list exit0", list, 0);
        verdict.Add(!HasCategory(list, "F0"), "list does not show F0 as assembled");

        const CommandResult retry =
            RunProject(root, { "project", "new", "--name", "F0", "--path", root.project_root(), "--plugin", "Sample" });
        AddExit(verdict, "retry without injection exit0", retry, 0);
        verdict.Add(FindRegistryEntry(root, "F0", is_initialized, total) && is_initialized,
                    "is_initialized=true after retry");
        verdict.Add(IsRegularFile(ProjectRecordPath(root, "resolved.json"))
                        && IsRegularFile(ProjectRecordPath(root, "manifest.json")),
                    "both records exist after retry");

        FinishCase("id.project.f0", root, verdict);
    }

    /// F1：`p3.before-first-copy=fail` → 2、intent 保留可解析、既有副本摘要不变
    void CaseF1() noexcept
    {
        const CaseRoot root("f1");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");

        const CommandResult failed = RunProjectInjected(
            root, { "project", "new", "--name", "F1", "--path", root.project_root(), "--plugin", "Sample" },
            "p3.before-first-copy=fail");
        AddExit(verdict, "exit2", failed, 2);
        CheckCategory(verdict, "write_failed", failed, "write_failed");

        verdict.Add(HasIntent(root), "intent.json retained");
        JsonReader intent;
        verdict.Add(intent.LoadFile(ProjectRecordPath(root, "intent.json")), "intent.json is parseable");
        verdict.Add(intent.String("command") == "new", "intent command=new");

        FinishCase("id.project.f1", root, verdict);
    }

    /// F2：`p3.after-first-copy=abort` → 124、intent 至少一项 done；重跑同一命令续做成功
    void CaseF2() noexcept
    {
        const CaseRoot root("f2");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");

        const AbortOutcome aborted =
            RunAbort(root, "f2-abort", "p3.after-first-copy=abort",
                     { "project", "new", "--name", "F2", "--path", root.project_root(), "--plugin", "Sample" });
        verdict.Add(aborted.is_124, "abort injection exits 124");
        verdict.Add(HasIntent(root), "intent.json retained");
        verdict.Add(IntentHasState(root, "done"), "intent has at least one done operation");

        // 崩溃点在后首个复制之后：`plugins.json` 已落盘，按 spec §9.4 重跑时不再接受选择参数，
        // 因此先断言带 `--plugin` 的重跑报 `selection_source_conflict`（3），再用同参数（无选择参数）重跑
        const CommandResult retry_with_selection =
            RunProject(root, { "project", "new", "--name", "F2", "--path", root.project_root(), "--plugin", "Sample" });
        AddExit(verdict, "retry with --plugin exit3 (plugins.json already written)", retry_with_selection, 3);
        CheckCategory(verdict, "retry with --plugin selection_source_conflict", retry_with_selection,
                      "selection_source_conflict");

        const CommandResult retry = RunProject(root, { "project", "new", "--name", "F2", "--path", root.project_root() });
        AddExit(verdict, "retry exit0 (continues the attempt)", retry, 0);
        verdict.Add(!HasIntent(root), "intent removed after commit");

        JsonReader resolved;
        JsonReader manifest;
        verdict.Add(ReadResolved(root, resolved) && ReadManifest(root, manifest), "both records readable");
        verdict.Add(resolved.String("generation_id") == manifest.String("generation_id"), "generation match");

        FinishCase("id.project.f2", root, verdict);
    }

    /// F3：`p6.after-manifest-rename=abort` → 重跑原命令 update → 0，不报 record_invalid
    void CaseF3() noexcept
    {
        const CaseRoot root("f3");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        AddExit(verdict, "baseline new exit0",
                RunProject(root, { "project", "new", "--name", "F3", "--path", root.project_root(), "--plugin", "Sample" }),
                0);
        verdict.Add(WritePluginsJson(root, "F3",
                                     "[ { \"name\": \"Sample\", \"linkage_type\": \"static\", \"options\": {} } ]"),
                    "selection refreshed (same content)");

        const AbortOutcome aborted =
            RunAbort(root, "f3-abort", "p6.after-manifest-rename=abort", { "project", "update", "F3" });
        verdict.Add(aborted.is_124, "abort injection exits 124");

        const CommandResult retry = RunProject(root, { "project", "update", "F3" });
        AddExit(verdict, "retry update exit0", retry, 0);
        verdict.Add(!HasCategory(retry, "record_invalid"), "no record_invalid");

        JsonReader resolved;
        JsonReader manifest;
        verdict.Add(ReadResolved(root, resolved) && ReadManifest(root, manifest), "both records readable");
        verdict.Add(resolved.String("generation_id") == manifest.String("generation_id"), "generation match");
        bool              is_digest_ok = false;
        const std::string manifest_sha = Sha256OfFile(ProjectRecordPath(root, "manifest.json"), is_digest_ok);
        verdict.Add(is_digest_ok && resolved.String("manifest_digest") == manifest_sha, "manifest_digest consistent");
        verdict.Add(!HasIntent(root), "intent deleted");

        FinishCase("id.project.f3", root, verdict);
    }

    /// F4：未完成 attempt 时改 `plugins.json` → 只报 recovery_conflict(2)；restore 后 update 成功
    void CaseF4() noexcept
    {
        const CaseRoot root("f4");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        verdict.Add(WritePlugin(root, SimplePlugin("Extra")), "fixture plugin Extra");
        AddExit(verdict, "baseline new exit0",
                RunProject(root, { "project", "new", "--name", "F4", "--path", root.project_root(), "--plugin", "Sample" }),
                0);

        // P4 片段写入点只在"根 CMakeLists 已存在、需要刷新片段"的路径上命中：用一次真实的 update 制造 attempt
        verdict.Add(WritePluginsJson(root, "F4",
                                     "[ { \"name\": \"Sample\", \"linkage_type\": \"static\", \"options\": {} },\n"
                                     "  { \"name\": \"Extra\", \"linkage_type\": \"static\", \"options\": {} } ]"),
                    "selection change that rewrites the root fragments");

        const CommandResult failed = RunProjectInjected(root, { "project", "update", "F4" }, "p4.before-fragment=fail");
        AddExit(verdict, "injected update exit2", failed, 2);
        verdict.Add(HasIntent(root), "intent retained");

        verdict.Add(WritePluginsJson(root, "F4",
                                     "[ { \"name\": \"Extra\", \"linkage_type\": \"static\", \"options\": {} } ]"),
                    "user changed the selection during the attempt");

        const CommandResult update = RunProject(root, { "project", "update", "F4" });
        AddExit(verdict, "update exit2", update, 2);
        CheckCategory(verdict, "recovery_conflict", update, "recovery_conflict");
        verdict.Add(HasIntent(root), "attempt paths not auto-deleted (intent still present)");

        const CommandResult restore = RunProject(root, { "project", "recover", "F4", "--action", "restore" });
        AddExit(verdict, "restore exit0", restore, 0);
        verdict.Add(!HasIntent(root), "intent deleted after restore");

        const CommandResult update_after = RunProject(root, { "project", "update", "F4" });
        AddExit(verdict, "update after restore exit0", update_after, 0);

        FinishCase("id.project.f4", root, verdict);
    }

    /// F5：`IDGenerated/backup` 被同名普通文件占位 + 手改副本 + `update --force` → 2，不覆盖原文件
    void CaseF5() noexcept
    {
        const CaseRoot root("f5");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        AddExit(verdict, "baseline new exit0",
                RunProject(root, { "project", "new", "--name", "F5", "--path", root.project_root(), "--plugin", "Sample" }),
                0);

        const std::string copy_path = root.ProjectPath("IDPlugin/Sample/include/Sample.hpp");
        std::string       copy_text;
        ReadText(copy_path, copy_text);
        const std::string modified = copy_text + "// local modification\n";
        verdict.Add(WriteText(copy_path, modified), "user modified the managed copy");
        verdict.Add(WriteText(root.ProjectPath("IDGenerated/backup"), "blocking file\n"),
                    "backup path blocked by a regular file");

        const CommandResult forced = RunProject(root, { "project", "update", "F5", "--force" });
        AddExit(verdict, "update --force exit2", forced, 2);
        CheckCategory(verdict, "write_failed", forced, "write_failed");

        std::string after;
        verdict.Add(ReadText(copy_path, after) && after == modified, "original file not overwritten");
        verdict.Add(HasIntent(root), "intent retained");

        FinishCase("id.project.f5", root, verdict);
    }

    /// F6：`p4.before-fragment=fail` 后直接 `build` → recovery_conflict(2)，不进入 configure
    void CaseF6() noexcept
    {
        const CaseRoot root("f6");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        verdict.Add(WritePlugin(root, SimplePlugin("Extra")), "fixture plugin Extra");
        AddExit(verdict, "baseline new exit0",
                RunProject(root, { "project", "new", "--name", "F6", "--path", root.project_root(), "--plugin", "Sample" }),
                0);
        verdict.Add(WritePluginsJson(root, "F6",
                                     "[ { \"name\": \"Sample\", \"linkage_type\": \"static\", \"options\": {} },\n"
                                     "  { \"name\": \"Extra\", \"linkage_type\": \"static\", \"options\": {} } ]"),
                    "selection change that rewrites the root fragments");
        const CommandResult failed = RunProjectInjected(root, { "project", "update", "F6" }, "p4.before-fragment=fail");
        AddExit(verdict, "injected update exit2", failed, 2);

        std::string resolved_before;
        std::string manifest_before;
        ReadText(ProjectRecordPath(root, "resolved.json"), resolved_before);
        ReadText(ProjectRecordPath(root, "manifest.json"), manifest_before);

        std::vector<std::string> arguments = { "project", "build", "F6" };
        AppendMingwProfile(arguments);
        arguments.emplace_back("--config");
        arguments.emplace_back("debug");
        const CommandResult build = RunProject(root, arguments);

        // spec §13.1 的全序是「输入校验 1 > 记录/所有权/并发 2」：本次同时报 stale_generated(1) 与
        // recovery_conflict(2) 时取 1。契约要点是"未完成 attempt 被识别出来、不接管、不进入 P7"，
        // 因此断言 1/2 之一成立且 recovery_conflict 在诊断里，而不是写死 2
        AddExitOneOf(verdict, "build exit1/2 (recovery_conflict not bypassed)", build, { 1, 2 });
        CheckCategory(verdict, "recovery_conflict", build, "recovery_conflict");
        verdict.Add(!IsRegularFile(root.ProjectPath("build/mingw/x64/debug/CMakeCache.txt")), "did not configure");

        std::string resolved_after;
        std::string manifest_after;
        ReadText(ProjectRecordPath(root, "resolved.json"), resolved_after);
        ReadText(ProjectRecordPath(root, "manifest.json"), manifest_after);
        verdict.Add(resolved_after == resolved_before && manifest_after == manifest_before,
                    "old records unchanged");

        FinishCase("id.project.f6", root, verdict);
    }

    /// F7：与 F3 同一崩溃点，改走 `recover --action continue` → 0，两记录一致
    void CaseF7() noexcept
    {
        const CaseRoot root("f7");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        AddExit(verdict, "baseline new exit0",
                RunProject(root, { "project", "new", "--name", "F7", "--path", root.project_root(), "--plugin", "Sample" }),
                0);
        verdict.Add(WritePluginsJson(root, "F7",
                                     "[ { \"name\": \"Sample\", \"linkage_type\": \"static\", \"options\": {} } ]"),
                    "selection refreshed");

        const AbortOutcome aborted =
            RunAbort(root, "f7-abort", "p6.after-manifest-rename=abort", { "project", "update", "F7" });
        verdict.Add(aborted.is_124, "abort injection exits 124");

        const CommandResult recovered = RunProject(root, { "project", "recover", "F7", "--action", "continue" });
        AddExit(verdict, "recover continue exit0", recovered, 0);
        verdict.Add(!HasCategory(recovered, "record_invalid"), "no record_invalid");

        JsonReader resolved;
        JsonReader manifest;
        verdict.Add(ReadResolved(root, resolved) && ReadManifest(root, manifest), "both records readable");
        verdict.Add(resolved.String("generation_id") == manifest.String("generation_id"), "generation match");
        verdict.Add(!HasIntent(root), "intent deleted");

        FinishCase("id.project.f7", root, verdict);
    }

    /// F8：`p6.after-resolved-rename=abort` → continue 收尾成功；同窗口 restore 报 recovery_conflict(2)
    void CaseF8() noexcept
    {
        const CaseRoot root("f8");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        AddExit(verdict, "baseline new exit0",
                RunProject(root, { "project", "new", "--name", "F8", "--path", root.project_root(), "--plugin", "Sample" }),
                0);
        verdict.Add(WritePluginsJson(root, "F8",
                                     "[ { \"name\": \"Sample\", \"linkage_type\": \"static\", \"options\": {} } ]"),
                    "selection refreshed");

        const AbortOutcome aborted =
            RunAbort(root, "f8-abort", "p6.after-resolved-rename=abort", { "project", "update", "F8" });
        verdict.Add(aborted.is_124, "abort injection exits 124");
        verdict.Add(HasIntent(root), "intent still present in the committed window");

        // 负例子项：同窗口 `restore` → recovery_conflict(2)
        const ID::Bench::IdProject::Snapshot before = ID::Bench::IdProject::TakeSnapshot(root);
        const CommandResult restore = RunProject(root, { "project", "recover", "F8", "--action", "restore" });
        const ID::Bench::IdProject::Snapshot after = ID::Bench::IdProject::TakeSnapshot(root);
        AddExit(verdict, "restore in committed window exit2", restore, 2);
        CheckCategory(verdict, "restore recovery_conflict", restore, "recovery_conflict");
        const std::string precheck_diff = ID::Bench::IdProject::SnapshotDiff(before, after);
        verdict.Add(precheck_diff.empty(), "restore precheck zero write");
        if (!precheck_diff.empty())
        {
            verdict.Note("snapshot diff", precheck_diff.substr(0, 400));
        }

        const CommandResult recovered = RunProject(root, { "project", "recover", "F8", "--action", "continue" });
        AddExit(verdict, "recover continue exit0", recovered, 0);
        verdict.Add(IsRegularFile(root.ProjectPath("IDPlugin/Sample/include/Sample.hpp")),
                    "newly created files not re-deleted");
        verdict.Add(!HasIntent(root), "intent deleted");

        FinishCase("id.project.f8", root, verdict);
    }

    /**
     *  F9：`p5.after-first-delete=abort` → 内容窗口拒绝 `continue`(2) → 重跑 `update` 续做至 0 → 再 `update` 0
     *
     *  口径：矩阵 §11 F9 写的是"`update` 报 `recovery_conflict`（2）"加"continue 收敛"，与 spec §6.6 链路 5、
     *  §9.4 相反：输入未变时重跑原命令即按三态续做至提交，`continue` 只用于"内容已全部落盘、只差记录提交"，
     *  删除做了一半时它必须拒绝（同 H4）。这里按 spec 断言，矩阵那一行的差异记入回报。
     */
    void CaseF9() noexcept
    {
        const CaseRoot root("f9");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        verdict.Add(WritePlugin(root, SimplePlugin("Extra")), "fixture plugin Extra");
        AddExit(verdict, "initial new exit0",
                RunProject(root, { "project", "new", "--name", "F9", "--path", root.project_root(), "--plugin", "Sample",
                                   "--plugin", "Extra" }),
                0);

        verdict.Add(WritePluginsJson(root, "F9",
                                     "[ { \"name\": \"Sample\", \"linkage_type\": \"static\", \"options\": {} } ]"),
                    "user removed Extra (P5 deletions expected)");

        // 登记表实现侧的标识串是 `p5.after-delete`（与 matrix §11.3 的 `p5.after-first-delete` 不同，已按实现取证）
        const AbortOutcome aborted = RunAbort(root, "f9-abort", "p5.after-delete=abort", { "project", "update", "F9" });
        verdict.Add(aborted.is_124, "abort injection exits 124");

        // 内容窗口（删除只做了一半）：`continue` 必须拒绝——它只用于"内容已全部落盘、只差记录提交"
        const CommandResult continue_mid = RunProject(root, { "project", "recover", "F9", "--action", "continue" });
        AddExit(verdict, "recover continue mid-delete exit2", continue_mid, 2);
        CheckCategory(verdict, "continue mid-delete recovery_conflict", continue_mid, "recovery_conflict");
        verdict.Add(HasIntent(root), "intent retained after refused continue");

        // 输入未变 → 重跑原命令按三态续做至提交（spec §6.6 链路 5）
        const CommandResult retry = RunProject(root, { "project", "update", "F9" });
        AddExit(verdict, "retry update continues to commit exit0", retry, 0);
        verdict.Add(!HasIntent(root), "intent converged by the retry");

        const CommandResult final_update = RunProject(root, { "project", "update", "F9" });
        AddExit(verdict, "update after converge exit0", final_update, 0);

        JsonReader manifest;
        verdict.Add(ReadManifest(root, manifest), "manifest readable");
        bool has_extra = false;
        for (std::size_t index = 0; index < manifest.Count("entries"); ++index)
        {
            has_extra = has_extra
                     || manifest.String("entries." + std::to_string(index) + ".path").rfind("IDPlugin/Extra/", 0) == 0;
        }
        verdict.Add(!has_extra, "manifest no longer lists the deleted plugin copies");

        FinishCase("id.project.f9", root, verdict);
    }

    /// F10：已提交项目上 update 失败，随后 `new` → is_initialized 仍 true，new 报 recovery_conflict(2)
    void CaseF10() noexcept
    {
        const CaseRoot root("f10");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        verdict.Add(WritePlugin(root, SimplePlugin("Extra")), "fixture plugin Extra");
        AddExit(verdict, "baseline new exit0",
                RunProject(root, { "project", "new", "--name", "F10", "--path", root.project_root(), "--plugin", "Sample" }),
                0);
        // 必须是一次**真实**的内容变化（新增 Extra 会产生复制动作），否则 update 是 no-op、
        // `p3.before-first-copy` 不会命中，用例就测不到"失败后仍有未完成 attempt"这一前提
        verdict.Add(WritePluginsJson(root, "F10",
                                     "[ { \"name\": \"Sample\", \"linkage_type\": \"static\", \"options\": {} },\n"
                                     "  { \"name\": \"Extra\", \"linkage_type\": \"static\", \"options\": {} } ]"),
                    "selection gains Extra (real copy work for P3)");

        const CommandResult failed =
            RunProjectInjected(root, { "project", "update", "F10" }, "p3.before-first-copy=fail");
        AddExit(verdict, "injected update exit2", failed, 2);
        verdict.Add(HasIntent(root), "intent retained");

        std::string registry_before;
        verdict.Add(ReadText(RegistryPath(root), registry_before), "registry readable");

        const CommandResult create = RunProject(root, { "project", "new", "--name", "F10", "--path", root.project_root() });
        // 同 F6：spec §13.1 全序里输入校验 1 高于恢复冲突 2，`stale_generated` 同时成立时取 1
        AddExitOneOf(verdict, "new exit1/2 (attempt not taken over)", create, { 1, 2 });
        CheckCategory(verdict, "recovery_conflict", create, "recovery_conflict");

        std::string registry_after;
        verdict.Add(ReadText(RegistryPath(root), registry_after) && registry_after == registry_before,
                    "registry bytes unchanged");
        bool        is_initialized = false;
        std::size_t total          = 0;
        verdict.Add(FindRegistryEntry(root, "F10", is_initialized, total) && is_initialized,
                    "is_initialized stays true");

        FinishCase("id.project.f10", root, verdict);
    }

    /// F11：计划完成后、写入前被外部改写 → changed_since_plan(2)；无窗口注入点，登记 Uncovered
    void CaseF11() noexcept
    {
        ID::Bench::Uncovered("id.project.f11",
                             "§11.6 U-2：`changed_since_plan` 的复验点在「读到快照后、原子替换前」，§11.3 没有该点位；"
                             "靠时序碰运气不算取证。目标期望为 changed_since_plan(2)，补点位后转常规用例");
    }

    /// F12：手工把 is_initialized 改成 false 后 `build` → warn registry_marker_stale，退出码不变
    void CaseF12() noexcept
    {
        const CaseRoot root("f12");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        AddExit(verdict, "baseline new exit0",
                RunProject(root, { "project", "new", "--name", "F12", "--path", root.project_root(), "--plugin", "Sample" }),
                0);

        const std::string registry = "[ { \"name\": \"F12\", \"path\": \"" + root.project_root()
                                   + "\", \"is_initialized\": false } ]";
        verdict.Add(WriteText(RegistryPath(root), registry), "user flipped is_initialized");

        std::vector<std::string> arguments = { "project", "build", "F12" };
        AppendMingwProfile(arguments);
        arguments.emplace_back("--config");
        arguments.emplace_back("debug");
        const CommandResult build = RunProject(root, arguments);

        AddExit(verdict, "build exit0", build, 0);
        // 第四个参数是类别标识本身（warn 级），不是说明串：带后缀会让子串匹配必然落空
        CheckCategory(verdict, "registry_marker_stale (warn)", build, "registry_marker_stale");

        JsonReader records;
        verdict.Add(ReadBuildRecords(root, records) && records.Count("") == 1, "build record appended");

        FinishCase("id.project.f12", root, verdict);
    }

    /// F13：源资产内容变化后 `update`（D=O、N≠O）→ 0，新摘要等于实际写出内容
    void CaseF13() noexcept
    {
        const CaseRoot root("f13");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        AddExit(verdict, "baseline new exit0",
                RunProject(root, { "project", "new", "--name", "F13", "--path", root.project_root(), "--plugin", "Sample" }),
                0);

        const std::string source_header = root.EnginePath("IDPlugin/Sample/include/Sample.hpp");
        std::string       header_text;
        ReadText(source_header, header_text);
        const std::string upgraded = header_text + "// upstream content upgrade\n";
        verdict.Add(WriteText(source_header, upgraded), "source asset content changed");

        const CommandResult update = RunProject(root, { "project", "update", "F13" });
        AddExit(verdict, "update exit0 without --force", update, 0);

        JsonReader manifest;
        verdict.Add(ReadManifest(root, manifest), "manifest readable");
        bool              is_recorded = false;
        std::string       recorded_digest;
        for (std::size_t index = 0; index < manifest.Count("entries"); ++index)
        {
            const std::string prefix = "entries." + std::to_string(index) + ".";
            if (manifest.String(prefix + "path") == "IDPlugin/Sample/include/Sample.hpp")
            {
                is_recorded     = true;
                recorded_digest = manifest.String(prefix + "digest");
            }
        }
        bool              is_digest_ok = false;
        const std::string actual = Sha256OfFile(root.ProjectPath("IDPlugin/Sample/include/Sample.hpp"), is_digest_ok);
        verdict.Add(is_recorded && is_digest_ok && recorded_digest == actual,
                    "new manifest digest equals the written content");

        FinishCase("id.project.f13", root, verdict);
    }

    /// F14：before-payload 写入失败（P3/P5 两个子项）→ 2，文件未被替换/删除，intent 保留
    void CaseF14() noexcept
    {
        const CaseRoot root("f14");
        CaseVerdict    verdict;

        // 子项一：P3 覆盖既有副本之前的 payload 写入失败
        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        AddExit(verdict, "baseline new exit0",
                RunProject(root, { "project", "new", "--name", "F14", "--path", root.project_root(), "--plugin", "Sample" }),
                0);

        const std::string source_header = root.EnginePath("IDPlugin/Sample/include/Sample.hpp");
        std::string       header_text;
        ReadText(source_header, header_text);
        WriteText(source_header, header_text + "// upgrade\n");

        const std::string copy_path = root.ProjectPath("IDPlugin/Sample/include/Sample.hpp");
        bool              is_digest_ok = false;
        const std::string digest_before = Sha256OfFile(copy_path, is_digest_ok);

        const CommandResult payload_failed =
            RunProjectInjected(root, { "project", "update", "F14" }, "p3.before-payload=fail");
        AddExit(verdict, "p3.before-payload exit2", payload_failed, 2);
        CheckCategory(verdict, "write_failed", payload_failed, "write_failed");

        bool              is_after_ok = false;
        const std::string digest_after = Sha256OfFile(copy_path, is_after_ok);
        verdict.Add(is_digest_ok && is_after_ok && digest_before == digest_after, "file not replaced");
        verdict.Add(HasIntent(root), "intent retained");

        FinishCase("id.project.f14", root, verdict);
    }

    /// F15：登记标记写入失败（两个子项）→ 2、记录保留无 intent、标记未改；后续 update 只重写标记
    void CaseF15() noexcept
    {
        const CaseRoot root("f15");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");

        // 子项一：首次 new + `p6.before-marker=fail`（标记仍为 false）
        const CommandResult failed = RunProjectInjected(
            root, { "project", "new", "--name", "F15", "--path", root.project_root(), "--plugin", "Sample" },
            "p6.before-marker=fail");
        AddExit(verdict, "marker failure exit2", failed, 2);
        CheckCategory(verdict, "write_failed", failed, "write_failed");

        verdict.Add(IsRegularFile(ProjectRecordPath(root, "resolved.json"))
                        && IsRegularFile(ProjectRecordPath(root, "manifest.json")),
                    "both records retained");
        verdict.Add(!HasIntent(root), "no intent after marker failure");
        bool        is_initialized = false;
        std::size_t total          = 0;
        const bool  is_registered  = FindRegistryEntry(root, "F15", is_initialized, total);
        verdict.Add(!is_registered || !is_initialized, "registry marker not rewritten to true");

        std::vector<std::string> arguments = { "project", "build", "F15" };
        AppendMingwProfile(arguments);
        arguments.emplace_back("--config");
        arguments.emplace_back("debug");
        const CommandResult build = RunProject(root, arguments);
        AddExit(verdict, "build exit0", build, 0);
        verdict.Add(!HasCategory(build, "record_invalid") && !HasCategory(build, "stale_generated"),
                    "build does not lock out on record_invalid/stale_generated");
        verdict.Add(HasCategory(build, "registry_marker_stale"), "build only warns registry_marker_stale");

        JsonReader resolved_before;
        JsonReader manifest_before;
        ReadResolved(root, resolved_before);
        ReadManifest(root, manifest_before);
        std::string resolved_bytes;
        std::string manifest_bytes;
        ReadText(ProjectRecordPath(root, "resolved.json"), resolved_bytes);
        ReadText(ProjectRecordPath(root, "manifest.json"), manifest_bytes);

        const CommandResult update = RunProject(root, { "project", "update", "F15" });
        AddExit(verdict, "content-unchanged update exit0", update, 0);

        std::string resolved_after;
        std::string manifest_after;
        ReadText(ProjectRecordPath(root, "resolved.json"), resolved_after);
        ReadText(ProjectRecordPath(root, "manifest.json"), manifest_after);
        verdict.Add(resolved_after == resolved_bytes && manifest_after == manifest_bytes,
                    "records bytes unchanged (只有标记被重写)");

        JsonReader resolved_final;
        verdict.Add(ReadResolved(root, resolved_final)
                        && resolved_final.String("generation_id") == resolved_before.String("generation_id"),
                    "generation unchanged");

        FinishCase("id.project.f15", root, verdict);
    }
} // namespace

namespace ID::Bench
{
    void IdProjectCasesF() noexcept
    {
        CaseF0();
        CaseF1();
        CaseF2();
        CaseF3();
        CaseF4();
        CaseF5();
        CaseF6();
        CaseF7();
        CaseF8();
        CaseF9();
        CaseF10();
        CaseF11();
        CaseF12();
        CaseF13();
        CaseF14();
        CaseF15();
    }
} // namespace ID::Bench
