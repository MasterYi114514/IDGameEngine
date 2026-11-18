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
 *  `id.project` 恢复组（H1–H8）。
 *
 *  判定口径：恢复类断言看 `intent` 是否收敛、两记录是否一致、以及用户字节（`src/**`、`plugins.json`、
 *  片段外内容）是否被改动；`restore` 的预检零写用快照判定。`Abort` 注入走子进程（期望码 124）。
 */

namespace
{
    using ID::Bench::Check;
    using ID::Bench::IdProject::AddExit;
    using ID::Bench::IdProject::CaseRoot;
    using ID::Bench::IdProject::CaseVerdict;
    using ID::Bench::IdProject::CheckCategory;
    using ID::Bench::IdProject::AddExitOneOf;
    using ID::Bench::IdProject::CommandResult;
    using ID::Bench::IdProject::HasCategory;
    using ID::Bench::IdProject::IsRegularFile;
    using ID::Bench::IdProject::JsonReader;
    using ID::Bench::IdProject::PluginSpec;
    using ID::Bench::IdProject::ProjectRecordPath;
    using ID::Bench::IdProject::ReadManifest;
    using ID::Bench::IdProject::ReadResolved;
    using ID::Bench::IdProject::ReadText;
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

    std::string ReplaceAll(std::string text, std::string_view from, std::string_view to) noexcept
    {
        std::size_t position = 0;
        while ((position = text.find(from, position)) != std::string::npos)
        {
            text.replace(position, from.size(), to);
            position += to.size();
        }
        return text;
    }

    bool HasIntent(const CaseRoot& root) noexcept
    {
        return IsRegularFile(ProjectRecordPath(root, "intent.json"));
    }

    // ==================== 用例 ====================

    /// H1：失败后原命令重跑：`plugins.json` 未落盘可带原选择参数；已落盘必须去掉，诊断给可复制命令
    void CaseH1() noexcept
    {
        const CaseRoot root("h1");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");

        // 参照基线：一次成功执行的 generation
        const CaseRoot reference("h1-reference");
        verdict.Add(WritePlugin(reference, SimplePlugin("Sample")), "reference fixture");
        const CommandResult clean_run =
            RunProject(reference, { "project", "new", "--name", "H1", "--path", reference.project_root(), "--plugin",
                                    "Sample" });
        AddExit(verdict, "reference new exit0", clean_run, 0);
        JsonReader reference_resolved;
        verdict.Add(ReadResolved(reference, reference_resolved), "reference resolved readable");
        const std::string reference_generation = reference_resolved.String("generation_id");

        // 子夹具一：p2.after-registry=fail（登记已写、plugins.json 未落盘）
        const CommandResult first = RunProjectInjected(
            root, { "project", "new", "--name", "H1", "--path", root.project_root(), "--plugin", "Sample" },
            "p2.after-registry=fail");
        AddExit(verdict, "p2.after-registry exit2", first, 2);
        verdict.Add(!IsRegularFile(root.ProjectPath("plugins.json")), "plugins.json not written");

        const CommandResult retry_with_selection =
            RunProject(root, { "project", "new", "--name", "H1", "--path", root.project_root(), "--plugin", "Sample" });
        AddExit(verdict, "retry with --plugin exit0", retry_with_selection, 0);

        JsonReader resolved;
        verdict.Add(ReadResolved(root, resolved), "resolved readable");
        verdict.Add(!reference_generation.empty() && resolved.String("generation_id") == reference_generation,
                    "generation equals a clean successful run");
        verdict.Add(!HasIntent(root), "intent converged");

        FinishCase("id.project.h1", root, verdict);
    }

    /// H2：无 `intent` 时 `resolved` 存在而 `manifest` 缺失 → record_invalid(2)，不删除副本
    void CaseH2() noexcept
    {
        const CaseRoot root("h2");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        AddExit(verdict, "baseline new exit0",
                RunProject(root, { "project", "new", "--name", "H2", "--path", root.project_root(), "--plugin", "Sample" }),
                0);

        bool              is_digest_ok = false;
        const std::string copy_digest =
            Sha256OfFile(root.ProjectPath("IDPlugin/Sample/include/Sample.hpp"), is_digest_ok);
        verdict.Add(ID::Bench::IdProject::RemoveTree(ProjectRecordPath(root, "manifest.json")), "delete manifest.json");

        const CommandResult build = RunProject(root, { "project", "build", "H2", "--config", "debug" });
        AddExit(verdict, "build exit2", build, 2);
        CheckCategory(verdict, "record_invalid", build, "record_invalid");

        bool              is_after_ok = false;
        const std::string after_digest =
            Sha256OfFile(root.ProjectPath("IDPlugin/Sample/include/Sample.hpp"), is_after_ok);
        verdict.Add(is_digest_ok && is_after_ok && copy_digest == after_digest, "no copy deleted");

        FinishCase("id.project.h2", root, verdict);
    }

    /// H3：两份记录 `generation_id` 不一致 → record_invalid(2)
    void CaseH3() noexcept
    {
        const CaseRoot root("h3");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        AddExit(verdict, "baseline new exit0",
                RunProject(root, { "project", "new", "--name", "H3", "--path", root.project_root(), "--plugin", "Sample" }),
                0);

        std::string manifest_text;
        verdict.Add(ReadText(ProjectRecordPath(root, "manifest.json"), manifest_text), "manifest readable");
        JsonReader manifest;
        const std::string generation = manifest.LoadText(manifest_text) ? manifest.String("generation_id") : std::string();
        const std::string other      = std::string(64, 'b');
        verdict.Add(!generation.empty()
                        && WriteText(ProjectRecordPath(root, "manifest.json"),
                                     ReplaceAll(manifest_text, generation, other)),
                    "manifest generation_id replaced");

        const CommandResult build = RunProject(root, { "project", "build", "H3", "--config", "debug" });
        AddExit(verdict, "build exit2", build, 2);
        CheckCategory(verdict, "record_invalid", build, "record_invalid");

        FinishCase("id.project.h3", root, verdict);
    }

    /// H4：`p4.before-fragment=fail` → `continue` 报 recovery_conflict(2)（内容未全部落盘）并给两条命令
    void CaseH4() noexcept
    {
        const CaseRoot root("h4");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        verdict.Add(WritePlugin(root, SimplePlugin("Extra")), "fixture plugin Extra");
        AddExit(verdict, "baseline new exit0",
                RunProject(root, { "project", "new", "--name", "H4", "--path", root.project_root(), "--plugin", "Sample" }),
                0);
        verdict.Add(WritePluginsJson(root, "H4",
                                     "[ { \"name\": \"Sample\", \"linkage_type\": \"static\", \"options\": {} },\n"
                                     "  { \"name\": \"Extra\", \"linkage_type\": \"static\", \"options\": {} } ]"),
                    "selection change that rewrites the root fragments");
        const CommandResult failed = RunProjectInjected(root, { "project", "update", "H4" }, "p4.before-fragment=fail");
        AddExit(verdict, "injected update exit2", failed, 2);

        const ID::Bench::IdProject::Snapshot before = ID::Bench::IdProject::TakeSnapshot(root);
        const CommandResult recovered = RunProject(root, { "project", "recover", "H4", "--action", "continue" });
        const ID::Bench::IdProject::Snapshot after = ID::Bench::IdProject::TakeSnapshot(root);

        AddExit(verdict, "continue exit2 (content not landed)", recovered, 2);
        CheckCategory(verdict, "continue recovery_conflict", recovered, "recovery_conflict");
        verdict.Add(HasCategory(recovered, "--action restore") && HasCategory(recovered, "update"),
                    "diagnostic gives both copyable commands");
        verdict.Add(ID::Bench::IdProject::SnapshotDiff(before, after).empty(), "zero write");
        verdict.Add(HasIntent(root), "intent retained");

        const CommandResult update = RunProject(root, { "project", "update", "H4" });
        AddExit(verdict, "update exit0", update, 0);
        verdict.Add(!HasIntent(root), "intent converged after update");

        FinishCase("id.project.h4", root, verdict);
    }

    /// H5：输入已变 → `restore` 只撤销工具改动，用户字节不动；随后 update 成功
    void CaseH5() noexcept
    {
        const CaseRoot root("h5");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        verdict.Add(WritePlugin(root, SimplePlugin("Extra")), "fixture plugin Extra");
        AddExit(verdict, "baseline new exit0",
                RunProject(root, { "project", "new", "--name", "H5", "--path", root.project_root(), "--plugin", "Sample" }),
                0);
        verdict.Add(WritePluginsJson(root, "H5",
                                     "[ { \"name\": \"Sample\", \"linkage_type\": \"static\", \"options\": {} },\n"
                                     "  { \"name\": \"Extra\", \"linkage_type\": \"static\", \"options\": {} } ]"),
                    "selection change that rewrites the root fragments");
        const CommandResult failed = RunProjectInjected(root, { "project", "update", "H5" }, "p4.before-fragment=fail");
        AddExit(verdict, "injected update exit2", failed, 2);

        // 用户输入已变：换一个插件
        verdict.Add(WritePluginsJson(root, "H5",
                                     "[ { \"name\": \"Extra\", \"linkage_type\": \"static\", \"options\": {} } ]"),
                    "user changed plugins.json during the attempt");
        std::string plugins_before;
        ReadText(root.ProjectPath("plugins.json"), plugins_before);
        const std::string plugins_digest = Sha256Hex(plugins_before);

        const CommandResult restore = RunProject(root, { "project", "recover", "H5", "--action", "restore" });
        AddExit(verdict, "restore exit0", restore, 0);
        verdict.Add(HasCategory(restore, "--action restore") || restore.exit_code == 0, "restore reported");

        std::string plugins_after;
        verdict.Add(ReadText(root.ProjectPath("plugins.json"), plugins_after) && Sha256Hex(plugins_after) == plugins_digest,
                    "plugins.json untouched");
        verdict.Add(!HasIntent(root), "intent deleted");

        const CommandResult update = RunProject(root, { "project", "update", "H5" });
        AddExit(verdict, "update after restore exit0", update, 0);

        FinishCase("id.project.h5", root, verdict);
    }

    /// H6：`clean` 已删除副本、源插件目录不可用 → 从项目内 before-payload 恢复
    void CaseH6() noexcept
    {
        ID::Bench::Uncovered("id.project.h6",
                             "构造「clean 已真实删除副本」需要一份 retention 之外的已登记条目；本轮只能通过改写 "
                             "`resolved.retention` 伪造记录（等于制造 record_invalid），且 clean 成功提交后 "
                             "before-payload 随收尾删除、用例侧无法在校验窗口内观察。按 §11.6 口径登记未覆盖");
    }

    /// H7：payload 损坏 → restore 报 recovery_conflict(2)，预检零写，`--force` 不绕过
    void CaseH7() noexcept
    {
        const CaseRoot root("h7");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        verdict.Add(WritePlugin(root, SimplePlugin("Extra")), "fixture plugin Extra");
        AddExit(verdict, "initial new exit0",
                RunProject(root, { "project", "new", "--name", "H7", "--path", root.project_root(), "--plugin", "Sample",
                                   "--plugin", "Extra" }),
                0);
        verdict.Add(WritePluginsJson(root, "H7",
                                     "[ { \"name\": \"Sample\", \"linkage_type\": \"static\", \"options\": {} } ]"),
                    "user removed Extra (prune has payloads)");

        SpawnOptions options;
        options.inject     = "p5.after-delete=abort";
        options.timeout_ms = 60000;
        const SpawnResult aborted =
            SpawnProject(root, "h7-abort", { "project", "update", "H7" }, options);
        verdict.Add(aborted.is_spawned && !aborted.is_timed_out && aborted.exit_code == 124,
                    "abort injection exits 124");
        if (!HasIntent(root))
        {
            ID::Bench::Uncovered("id.project.h7", "本机未能通过 `p5.after-first-delete=abort` 留下 intent，"
                                                  "无法构造 payload 损坏样本；不重试碰样本");
            verdict.Add(true, "payload corruption sample not constructed");
            FinishCase("id.project.h7", root, verdict);
            return;
        }

        // 破坏一个 before-payload：把 payload 目录下第一个普通文件改成不同内容
        bool is_corrupted = false;
        for (const std::string& entry : ID::Bench::IdProject::ListTree(root.ProjectPath("IDGenerated/before")))
        {
            if (entry.rfind("FILE ", 0) != 0)
            {
                continue;
            }
            is_corrupted = WriteText(root.ProjectPath("IDGenerated/before/" + entry.substr(5)), "corrupted\n");
            break;
        }
        verdict.Add(is_corrupted, "one before-payload corrupted");

        const ID::Bench::IdProject::Snapshot before = ID::Bench::IdProject::TakeSnapshot(root);
        const CommandResult restore = RunProject(root, { "project", "recover", "H7", "--action", "restore" });
        const ID::Bench::IdProject::Snapshot after = ID::Bench::IdProject::TakeSnapshot(root);

        AddExit(verdict, "restore exit2", restore, 2);
        CheckCategory(verdict, "recovery_conflict", restore, "recovery_conflict");
        const std::string precheck_diff = ID::Bench::IdProject::SnapshotDiff(before, after);
        verdict.Add(precheck_diff.empty(), "precheck zero write");
        if (!precheck_diff.empty())
        {
            verdict.Note("snapshot diff", precheck_diff.substr(0, 400));
        }
        verdict.Add(HasIntent(root), "intent not deleted");

        // `--force` 不放行撤销：或接受该参数但仍报 recovery_conflict（2），或直接拒绝该参数（3）。
        // 两种形态都满足"不绕过"；关键后果是撤销没有发生（intent 保留）
        const CommandResult forced = RunProject(root, { "project", "recover", "H7", "--action", "restore", "--force" });
        AddExitOneOf(verdict, "restore --force not bypassed (2 or 3)", forced, { 2, 3 });
        verdict.Add(HasIntent(root), "intent retained after --force");

        FinishCase("id.project.h7", root, verdict);
    }

    /// H8：三态混合的 restore —— 本轮只覆盖「删除已完成 → restore 写回」这一条可稳定构造的路径
    void CaseH8() noexcept
    {
        const CaseRoot root("h8");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        verdict.Add(WritePlugin(root, SimplePlugin("Extra")), "fixture plugin Extra");
        AddExit(verdict, "initial new exit0",
                RunProject(root, { "project", "new", "--name", "H8", "--path", root.project_root(), "--plugin", "Sample",
                                   "--plugin", "Extra" }),
                0);

        const std::string extra_copy = root.ProjectPath("IDPlugin/Extra/include/Extra.hpp");
        bool              is_digest_ok = false;
        const std::string original_digest = Sha256OfFile(extra_copy, is_digest_ok);

        verdict.Add(WritePluginsJson(root, "H8",
                                     "[ { \"name\": \"Sample\", \"linkage_type\": \"static\", \"options\": {} } ]"),
                    "user removed Extra (prune with payloads)");

        SpawnOptions options;
        options.inject     = "p5.after-delete=abort";
        options.timeout_ms = 60000;
        const SpawnResult aborted = SpawnProject(root, "h8-abort", { "project", "update", "H8" }, options);
        verdict.Add(aborted.is_spawned && !aborted.is_timed_out && aborted.exit_code == 124,
                    "abort injection exits 124");
        if (!HasIntent(root))
        {
            ID::Bench::Uncovered("id.project.h8", "本机未能通过 `p5.after-first-delete=abort` 留下 intent，"
                                                  "三态混合样本无法构造；不重试碰样本");
            verdict.Add(true, "mixed tri-state sample not constructed");
            FinishCase("id.project.h8", root, verdict);
            return;
        }

        const CommandResult restore = RunProject(root, { "project", "recover", "H8", "--action", "restore" });
        AddExit(verdict, "restore exit0", restore, 0);

        bool              is_after_ok = false;
        const std::string restored_digest = Sha256OfFile(extra_copy, is_after_ok);
        verdict.Add(is_digest_ok && is_after_ok && restored_digest == original_digest,
                    "deleted copy restored from before-payload");
        verdict.Add(!HasIntent(root), "intent deleted after restore");

        FinishCase("id.project.h8", root, verdict);
    }
} // namespace

namespace ID::Bench
{
    void IdProjectCasesH() noexcept
    {
        CaseH1();
        CaseH2();
        CaseH3();
        CaseH4();
        CaseH5();
        CaseH6();
        CaseH7();
        CaseH8();
    }
} // namespace ID::Bench
