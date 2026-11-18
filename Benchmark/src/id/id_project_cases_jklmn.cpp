#include <cstddef>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "benchmark_id.hpp"
#include "benchmark_support.hpp"
#include "id_project_cases.hpp"
#include "id_project_test_support.hpp"

/**
 *  `id.project` 路径安全 / 裁剪 / 记录 / 命名 / 并发组（J1–J3、K1–K6、L1–L5、M1–M3、N1–N4）。
 *
 *  记录类负例（J1、L2、L3）按 `docs/id/11-verification-matrix.md` §11.5 的构造规则处理：先跑一次真实命令
 *  拿到自洽基线，再局部改写需要"故意不一致"的字段；改写后按测试侧独立 SHA-256 重算 `manifest` 摘要并
 *  同步 `resolved.manifest_digest`，避免第一跳就撞 `record_invalid` 而测不到目标行。
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
    using ID::Bench::IdProject::CopyTree;
    using ID::Bench::IdProject::CreateDirectories;
    using ID::Bench::IdProject::CreateJunction;
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
    using ID::Bench::IdProject::RegistryPath;
    using ID::Bench::IdProject::RemoveTree;
    using ID::Bench::IdProject::RunProject;
    using ID::Bench::IdProject::RunProjectInjected;
    using ID::Bench::IdProject::Sha256Hex;
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

    /// @brief `needle` 在 `text` 中逐字出现的次数（空 needle 返回 0）
    std::size_t CountOccurrences(std::string_view text, std::string_view needle) noexcept
    {
        if (needle.empty() || needle.size() > text.size())
        {
            return 0;
        }
        std::size_t count  = 0;
        std::size_t cursor = 0;
        while ((cursor = text.find(needle, cursor)) != std::string_view::npos)
        {
            ++count;
            cursor += needle.size();
        }
        return count;
    }

    /**
     *  局部改写 `manifest.json` 后让记录重新自洽：重算摘要并同步 `resolved.manifest_digest`。
     *  返回 false 表示改写或落盘失败（调用方按夹具构造失败记录）。
     */
    bool RewriteManifestAndSync(CaseVerdict& verdict, const CaseRoot& root, std::string_view from,
                                std::string_view to) noexcept
    {
        std::string manifest_text;
        if (!ReadText(ProjectRecordPath(root, "manifest.json"), manifest_text))
        {
            return false;
        }

        const std::string rewritten = ReplaceAll(manifest_text, from, to);
        if (!WriteText(ProjectRecordPath(root, "manifest.json"), rewritten))
        {
            return false;
        }

        std::string resolved_text;
        if (!ReadText(ProjectRecordPath(root, "resolved.json"), resolved_text))
        {
            return false;
        }

        const std::string old_digest = Sha256Hex(manifest_text);
        const std::string new_digest = Sha256Hex(rewritten);
        resolved_text                = ReplaceAll(resolved_text, old_digest, new_digest);
        verdict.Add(WriteText(ProjectRecordPath(root, "resolved.json"), resolved_text), "sync resolved.manifest_digest");
        return true;
    }

    /// @brief 建一个已提交项目（`--plugin Sample`），返回是否成功
    bool PrepareCommitted(CaseVerdict& verdict, const CaseRoot& root, std::string_view name) noexcept
    {
        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        const CommandResult create =
            RunProject(root, { "project", "new", "--name", std::string(name), "--path", root.project_root(),
                               "--plugin", "Sample" });
        return AddExit(verdict, "baseline new exit0", create, 0);
    }

    // ==================== 路径安全 ====================

    /// J1：`manifest` 条目含 `..` / 绝对路径 / `C:foo` / `CON` → path_outside_project(1)，不执行删除
    void CaseJ1() noexcept
    {
        const CaseRoot root("j1");
        CaseVerdict    verdict;

        verdict.Add(PrepareCommitted(verdict, root, "J1"), "committed baseline");

        const std::string original = "IDPlugin/Sample/include/Sample.hpp";
        const std::string samples[4] = { "IDPlugin/../outside.txt", root.CasePath("absolute.txt"), "C:foo.txt", "CON" };
        const char* const labels[4]  = { "dotdot", "absolute", "drive-relative", "reserved-name" };

        for (std::size_t index = 0; index < 4; ++index)
        {
            verdict.Add(RewriteManifestAndSync(verdict, root, original, samples[index]),
                        std::string(labels[index]) + ".rewrite");
            const CommandResult clean = RunProject(root, { "project", "clean", "J1" });
            ID::Bench::IdProject::AddExit(verdict, std::string(labels[index]) + ".exit1", clean, 1);
            ID::Bench::IdProject::CheckCategory(verdict, std::string(labels[index]) + ".path_outside_project", clean,
                                                "path_outside_project");
            verdict.Add(IsRegularFile(root.ProjectPath(original)), std::string(labels[index]) + ".no deletion");

            // 复原到基线，保证下一个样本在同一基线上构造
            verdict.Add(RewriteManifestAndSync(verdict, root, samples[index], original),
                        std::string(labels[index]) + ".restore");
        }

        FinishCase("id.project.j1", root, verdict);
    }

    /// J2：项目内 `IDPlugin` 是指向工程外的 junction → path_outside_project(1)
    void CaseJ2() noexcept
    {
        const CaseRoot root("j2");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");

        // 夹具：先用正常路径建好项目，再把项目内 IDPlugin 换成指向用例根之外的 junction
        const CommandResult create =
            RunProject(root, { "project", "new", "--name", "J2", "--path", root.project_root(), "--plugin", "Sample" });
        AddExit(verdict, "baseline new exit0", create, 0);

        const std::string outside = root.CasePath("outside-plugin-tree");
        verdict.Add(CreateDirectories(outside), "outside tree fixture");
        verdict.Add(WriteText(outside + "/marker.txt", "outside\n"), "outside marker");
        verdict.Add(RemoveTree(root.ProjectPath("IDPlugin")), "remove project IDPlugin");
        const bool is_junction = CreateJunction(root.ProjectPath("IDPlugin"), outside);
        if (!is_junction)
        {
            ID::Bench::Uncovered("id.project.j2", "环境不可构造：`cmd /c mklink /J` 未能创建 junction（§11.6 U-5），"
                                                 "该用例不参与判定");
            verdict.Add(true, "junction uncovered");
        }
        else
        {
            const CommandResult update = RunProject(root, { "project", "update", "J2" });
            AddExit(verdict, "update exit1", update, 1);
            CheckCategory(verdict, "path_outside_project", update, "path_outside_project");
        }

        FinishCase("id.project.j2", root, verdict);
    }

    /// J3：目标目录非空且无登记，加 `--force` → 仍 project_exists(1)，不接管
    void CaseJ3() noexcept
    {
        const CaseRoot root("j3");
        CaseVerdict    verdict;

        verdict.Add(WriteText(root.ProjectPath("user-file.txt"), "user content\n"), "non-empty unregistered directory");
        const std::string before = Sha256Hex("user content\n");

        const CommandResult result =
            RunProject(root, { "project", "new", "--name", "J3", "--path", root.project_root(), "--empty", "--force" });
        AddExit(verdict, "exit1", result, 1);
        CheckCategory(verdict, "project_exists", result, "project_exists");

        std::string after;
        verdict.Add(ReadText(root.ProjectPath("user-file.txt"), after) && Sha256Hex(after) == before,
                    "directory content unchanged");

        FinishCase("id.project.j3", root, verdict);
    }

    // ==================== 裁剪 ====================

    /// K1：上一代存在已删插件的副本 → update 后 IDPlugin 只剩保留集，用户源码与记录完整
    void CaseK1() noexcept
    {
        const CaseRoot root("k1");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        verdict.Add(WritePlugin(root, SimplePlugin("Extra")), "fixture plugin Extra");
        AddExit(verdict, "initial new exit0",
                RunProject(root, { "project", "new", "--name", "K1", "--path", root.project_root(), "--plugin", "Sample",
                                   "--plugin", "Extra" }),
                0);

        std::string src_before;
        std::string records_before;
        ReadText(root.ProjectPath("src/main.cpp"), src_before);
        ReadText(ProjectRecordPath(root, "resolved.json"), records_before);

        verdict.Add(WritePluginsJson(root, "K1",
                                     "[ { \"name\": \"Sample\", \"linkage_type\": \"static\", \"options\": {} } ]"),
                    "user removed Extra from the selection");
        AddExit(verdict, "update exit0", RunProject(root, { "project", "update", "K1" }), 0);

        verdict.Add(!IsDirectory(root.ProjectPath("IDPlugin/Extra")), "IDPlugin/Extra removed");
        verdict.Add(IsDirectory(root.ProjectPath("IDPlugin/Sample")), "IDPlugin/Sample kept");

        JsonReader manifest;
        verdict.Add(ReadManifest(root, manifest), "manifest readable");
        bool has_extra = false;
        for (std::size_t index = 0; index < manifest.Count("entries"); ++index)
        {
            has_extra = has_extra
                     || manifest.String("entries." + std::to_string(index) + ".path").rfind("IDPlugin/Extra/", 0) == 0;
        }
        verdict.Add(!has_extra, "manifest has no IDPlugin/Extra/** entry");

        std::string src_after;
        verdict.Add(ReadText(root.ProjectPath("src/main.cpp"), src_after) && src_after == src_before,
                    "src/main.cpp untouched");
        verdict.Add(IsDirectory(root.ProjectPath("IDGenerated")), "IDGenerated kept");

        FinishCase("id.project.k1", root, verdict);
    }

    /**
     *  K2：裁剪的强制备份语义 + 删除前的 before-payload 窗口
     *
     *  两段取证：
     *  1. 手改受管副本后 `clean` → `cleanup_blocked`（5），零删除；`clean --force` → 0，并在
     *     `IDGenerated/backup/` 留下手改后的字节（保留集内被本地修改的条目本次不覆盖、不删除）。
     *  2. 删除前的 before-payload 只能在删除窗口内取证：`clean` 在正常项目上没有删除对象（保留集取自
     *     `resolved.retention`，`Plan/Planner.cpp` 的裁剪循环只处理保留集外条目），payload 又随提交收尾
     *     删除、事后无法观察。因此删除窗口用 `update` 的裁剪构造（与 clean 共用同一条 P5 删除路径），并在
     *     首个删除之前用 `p5.before-delete`（登记串见 `ID/src/project/Base/Inject.cpp`）中止，断言
     *     `IDGenerated/before/<attempt>/...` 存在且内容等于删除前的磁盘字节，最后 `recover --action restore`。
     */
    void CaseK2() noexcept
    {
        const CaseRoot root("k2");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        verdict.Add(WritePlugin(root, SimplePlugin("Extra")), "fixture plugin Extra");
        AddExit(verdict, "baseline new exit0",
                RunProject(root, { "project", "new", "--name", "K2", "--path", root.project_root(), "--plugin", "Sample",
                                   "--plugin", "Extra" }),
                0);

        // 段一：手改副本 → clean 阻断；clean --force 备份后放行
        const std::string copy_path = root.ProjectPath("IDPlugin/Sample/include/Sample.hpp");
        std::string       copy_text;
        verdict.Add(ReadText(copy_path, copy_text), "managed copy readable");
        const std::string modified = copy_text + "// local modification\n";
        verdict.Add(WriteText(copy_path, modified), "user modified the managed copy");
        const std::string modified_digest = Sha256Hex(modified);

        const CommandResult blocked = RunProject(root, { "project", "clean", "K2" });
        AddExit(verdict, "clean exit5", blocked, 5);
        CheckCategory(verdict, "cleanup_blocked", blocked, "cleanup_blocked");
        std::string kept_text;
        verdict.Add(ReadText(copy_path, kept_text) && Sha256Hex(kept_text) == modified_digest,
                    "blocked clean keeps the modified copy");

        const CommandResult forced = RunProject(root, { "project", "clean", "K2", "--force" });
        AddExit(verdict, "clean --force exit0", forced, 0);

        bool is_backup_found = false;
        for (const std::string& entry : ListTree(root.ProjectPath("IDGenerated/backup")))
        {
            if (entry.rfind("FILE ", 0) != 0)
            {
                continue;
            }
            std::string backup_text;
            if (ReadText(root.ProjectPath("IDGenerated/backup/" + entry.substr(5)), backup_text)
                && Sha256Hex(backup_text) == modified_digest)
            {
                is_backup_found = true;
            }
        }
        verdict.Add(is_backup_found, "backup keeps the abandoned local modification");

        // 段二：删除前的 before-payload。用户放弃手改先写回原字节，否则 update 会先撞写入冲突、
        // 到不了删除阶段
        verdict.Add(WriteText(copy_path, copy_text), "user reverted the modification before update");
        verdict.Add(WritePluginsJson(root, "K2",
                                     "[ { \"name\": \"Sample\", \"linkage_type\": \"static\", \"options\": {} } ]"),
                    "user removed Extra from the selection");

        const std::string extra_copy   = root.ProjectPath("IDPlugin/Extra/include/Extra.hpp");
        bool              is_extra_ok  = false;
        const std::string extra_digest = Sha256OfFile(extra_copy, is_extra_ok);

        const CommandResult stopped = RunProjectInjected(root, { "project", "update", "K2" }, "p5.before-delete=fail");
        AddExit(verdict, "update stopped before the first delete exit2", stopped, 2);
        verdict.Add(is_extra_ok && IsRegularFile(extra_copy), "no byte removed at the stop point");

        std::string intent_text;
        JsonReader  intent;
        const bool  is_intent_ok =
            ReadText(ProjectRecordPath(root, "intent.json"), intent_text) && intent.LoadText(intent_text);
        verdict.Add(is_intent_ok, "intent.json readable in the delete window");
        const std::string attempt = is_intent_ok ? intent.String("attempt") : std::string();
        verdict.Add(!attempt.empty(), "intent carries the attempt id");

        // 删除项的 payload 与"删除前磁盘字节"同源：`intent.operations[]` 里 `after = null` 的项就是删除项，
        // 其 `payload` 指向 `IDGenerated/before/<attempt>/<path>`，`before` 是删除前摘要。
        // 写入项（本次被重写的聚合/插件 CMakeLists）的 payload 也应存在，但磁盘字节已被本次写入取代，
        // 不能用"等于当前磁盘字节"判它。
        std::size_t delete_payloads = 0;
        bool        is_delete_payload_equal = true;
        bool        has_extra_payload       = false;
        for (std::size_t index = 0; index < intent.Count("operations"); ++index)
        {
            const std::string prefix = "operations." + std::to_string(index) + ".";
            const ID::Json::JsonValue* after_node = intent.Find(prefix + "after");
            const std::string payload = intent.String(prefix + "payload");
            const std::string before  = intent.String(prefix + "before");
            if (payload.empty() || before.empty() || after_node == nullptr || !after_node->is_null())
            {
                continue;   // 只判删除项（after = absent）
            }
            std::string payload_bytes;
            std::string disk_bytes;
            if (!ReadText(root.ProjectPath(payload), payload_bytes))
            {
                is_delete_payload_equal = false;
                continue;
            }
            ++delete_payloads;
            is_delete_payload_equal = is_delete_payload_equal && Sha256Hex(payload_bytes) == before
                                   && ReadText(root.ProjectPath(intent.String(prefix + "path")), disk_bytes)
                                   && Sha256Hex(disk_bytes) == before;
            if (intent.String(prefix + "path") == "IDPlugin/Extra/include/Extra.hpp")
            {
                has_extra_payload = Sha256Hex(payload_bytes) == extra_digest;
            }
        }
        verdict.Add(delete_payloads > 0, "before-payload written for the pending deletes inside the window");
        verdict.Add(is_delete_payload_equal, "every delete payload equals the pre-delete bytes (payload and disk)");
        verdict.Add(has_extra_payload, "Extra.hpp payload equals its pre-delete bytes");

        const CommandResult restore = RunProject(root, { "project", "recover", "K2", "--action", "restore" });
        AddExit(verdict, "restore exit0", restore, 0);
        verdict.Add(!IsRegularFile(ProjectRecordPath(root, "intent.json")), "intent converged after restore");
        bool              is_after_ok = false;
        const std::string extra_after = Sha256OfFile(extra_copy, is_after_ok);
        verdict.Add(is_extra_ok && is_after_ok && extra_after == extra_digest, "pruned copy kept by restore");

        FinishCase("id.project.k2", root, verdict);
    }

    /// K3：`IDPlugin/` 有未登记文件时 `clean` 与 `clean --force` 都 cleanup_blocked(5)，零删除、不写 intent
    void CaseK3() noexcept
    {
        const CaseRoot root("k3");
        CaseVerdict    verdict;

        verdict.Add(PrepareCommitted(verdict, root, "K3"), "committed baseline");

        const std::string user_path = root.ProjectPath("IDPlugin/K3-NOTE.txt");
        verdict.Add(WriteText(user_path, "user note\n"), "unregistered file fixture");

        const ID::Bench::IdProject::Snapshot before = TakeSnapshot(root);
        const CommandResult clean  = RunProject(root, { "project", "clean", "K3" });
        const CommandResult forced = RunProject(root, { "project", "clean", "K3", "--force" });
        const ID::Bench::IdProject::Snapshot after = TakeSnapshot(root);

        AddExit(verdict, "clean exit5", clean, 5);
        AddExit(verdict, "clean --force exit5", forced, 5);
        verdict.Add(!IsRegularFile(ProjectRecordPath(root, "intent.json")), "no intent written");
        verdict.Add(SnapshotDiff(before, after).empty(), "zero deletion / zero commit");
        verdict.Add(IsRegularFile(user_path), "unregistered file kept");

        FinishCase("id.project.k3", root, verdict);
    }

    /// K4：源插件目录不可用、`plugins.json` 被改后 `clean --check` → 0 或 6，不访问源目录
    void CaseK4() noexcept
    {
        const CaseRoot root("k4");
        CaseVerdict    verdict;

        verdict.Add(PrepareCommitted(verdict, root, "K4"), "committed baseline");

        verdict.Add(RemoveTree(root.EnginePath("IDPlugin/Sample")), "source plugin moved away");
        verdict.Add(WritePluginsJson(root, "K4", "[]"), "user changed plugins.json");

        const ID::Bench::IdProject::Snapshot before = TakeSnapshot(root);
        const CommandResult check                   = RunProject(root, { "project", "clean", "K4", "--check" });
        const ID::Bench::IdProject::Snapshot after  = TakeSnapshot(root);

        verdict.Add(check.exit_code == 0 || check.exit_code == 6, "check exit 0 or 6");
        verdict.Add(SnapshotDiff(before, after).empty(), "zero write");
        verdict.Add(!IsDirectory(root.EnginePath("IDPlugin/Sample")), "source directory still absent (not accessed)");
        verdict.Add(!HasCategory(check, "asset_missing"), "no asset_missing");
        verdict.Add(!HasCategory(check, "plugin_unknown"), "no plugin_unknown");

        FinishCase("id.project.k4", root, verdict);
    }

    /// K5：`clean` 后紧接 `build` → 0，generation 未变，manifest_digest 与磁盘一致，无记录/冲突诊断
    void CaseK5() noexcept
    {
        const CaseRoot root("k5");
        CaseVerdict    verdict;

        verdict.Add(PrepareCommitted(verdict, root, "K5"), "committed baseline");

        JsonReader resolved_before;
        verdict.Add(ReadResolved(root, resolved_before), "resolved readable");
        const std::string generation_before = resolved_before.String("generation_id");

        const CommandResult clean = RunProject(root, { "project", "clean", "K5" });
        verdict.Add(clean.exit_code == 0 || clean.exit_code == 6, "clean exit 0 or 6");

        std::vector<std::string> build_arguments = { "project", "build", "K5" };
        AppendMingwProfile(build_arguments);
        build_arguments.emplace_back("--config");
        build_arguments.emplace_back("debug");
        const CommandResult build = RunProject(root, build_arguments);
        AddExit(verdict, "build exit0", build, 0);
        verdict.Add(!HasCategory(build, "record_invalid"), "no record_invalid");
        verdict.Add(!HasCategory(build, "unmanaged_content"), "no unmanaged_content");

        JsonReader resolved_after;
        verdict.Add(ReadResolved(root, resolved_after), "resolved readable after clean");
        verdict.Add(resolved_after.String("generation_id") == generation_before, "generation_id unchanged");

        bool              is_digest_ok = false;
        const std::string manifest_sha = Sha256OfFile(ProjectRecordPath(root, "manifest.json"), is_digest_ok);
        verdict.Add(is_digest_ok && resolved_after.String("manifest_digest") == manifest_sha,
                    "resolved.manifest_digest == sha256(manifest.json)");

        ID::Bench::Uncovered("id.project.k5.real-delete",
                             "构造「真实的待删条目」需要改写 `resolved.retention`（等于伪造记录），本轮按 §11.6 口径"
                             "登记未覆盖；clean 的 no-op 路径与后续 build 已判定");

        FinishCase("id.project.k5", root, verdict);
    }

    /// K6：no-op `clean` 不写 intent、不改记录（字节与 mtime 都不变）
    void CaseK6() noexcept
    {
        const CaseRoot root("k6");
        CaseVerdict    verdict;

        verdict.Add(PrepareCommitted(verdict, root, "K6"), "committed baseline");

        const ID::Bench::IdProject::Snapshot before = TakeSnapshot(root);
        const CommandResult clean                   = RunProject(root, { "project", "clean", "K6" });
        const ID::Bench::IdProject::Snapshot after  = TakeSnapshot(root);

        verdict.Add(clean.exit_code == 0 || clean.exit_code == 6, "clean exit 0 or 6");
        const std::string diff = SnapshotDiff(before, after);
        verdict.Add(diff.empty(), "records and mtime unchanged");
        if (!diff.empty())
        {
            verdict.Note("diff", diff.substr(0, 400));
        }
        verdict.Add(!IsRegularFile(ProjectRecordPath(root, "intent.json")), "no intent written");

        FinishCase("id.project.k6", root, verdict);
    }

    // ==================== 记录 ====================

    /// L1：`manifest` 覆盖两个根片段、生成物、复制物与 `seed`，且不含记录/日志/锁/备份/构建/产物
    void CaseL1() noexcept
    {
        const CaseRoot root("l1");
        CaseVerdict    verdict;

        verdict.Add(PrepareCommitted(verdict, root, "L1"), "committed baseline");

        JsonReader manifest;
        verdict.Add(ReadManifest(root, manifest), "manifest readable");

        std::size_t fragment_entries = 0;
        bool        has_copied = false;
        bool        has_generated = false;
        bool        has_seed = false;
        bool        has_forbidden = false;
        std::vector<std::string> identities;

        const std::size_t count = manifest.Count("entries");
        for (std::size_t index = 0; index < count; ++index)
        {
            const std::string prefix  = "entries." + std::to_string(index) + ".";
            const std::string path    = manifest.String(prefix + "path");
            const std::string kind    = manifest.String(prefix + "kind");
            const ID::Json::JsonValue* fragment = manifest.Find(prefix + "fragment");
            std::string fragment_text = (fragment != nullptr && fragment->is_string()) ? std::string(fragment->as_string())
                                                                                      : std::string();

            if (!fragment_text.empty())
            {
                ++fragment_entries;
            }
            has_copied    = has_copied || kind == "copied";
            has_generated = has_generated || kind == "generated";
            has_seed      = has_seed || kind == "seed";

            if (path.rfind("IDGenerated/", 0) == 0 || path.rfind("log/", 0) == 0 || path == "plugins.json"
                || path.rfind("build/", 0) == 0 || path.rfind("bin/", 0) == 0
                || path.find(".lock") != std::string::npos || path.rfind("backup/", 0) == 0)
            {
                has_forbidden = true;
            }

            identities.push_back(path + "#" + fragment_text);
        }

        verdict.Add(fragment_entries == 2, "two root fragment entries");
        verdict.Add(has_copied, "copied entries present");
        verdict.Add(has_generated, "generated entries present");
        verdict.Add(has_seed, "seed entry present");
        verdict.Add(!has_forbidden, "no record/log/lock/backup/build/bin entry");

        bool is_unique = true;
        for (std::size_t left = 0; left < identities.size(); ++left)
        {
            for (std::size_t right = left + 1; right < identities.size(); ++right)
            {
                if (identities[left] == identities[right])
                {
                    is_unique = false;
                }
            }
        }
        verdict.Add(is_unique, "(path, fragment) identities unique");

        FinishCase("id.project.l1", root, verdict);
    }

    /// L2：`resolved.manifest_digest` 与实际不符 → record_invalid(2)，不删除副本
    void CaseL2() noexcept
    {
        const CaseRoot root("l2");
        CaseVerdict    verdict;

        verdict.Add(PrepareCommitted(verdict, root, "L2"), "committed baseline");

        std::string resolved_text;
        verdict.Add(ReadText(ProjectRecordPath(root, "resolved.json"), resolved_text), "resolved readable");
        JsonReader resolved;
        const std::string digest = resolved.LoadText(resolved_text) ? resolved.String("manifest_digest") : std::string();
        const std::string bad    = std::string(64, 'a');
        verdict.Add(!digest.empty() && WriteText(ProjectRecordPath(root, "resolved.json"),
                                                 ReplaceAll(resolved_text, digest, bad)),
                    "resolved.manifest_digest replaced with a wrong digest");

        const CommandResult build = RunProject(root, { "project", "build", "L2", "--config", "debug" });
        AddExit(verdict, "build exit2", build, 2);
        CheckCategory(verdict, "record_invalid", build, "record_invalid");
        verdict.Add(IsRegularFile(root.ProjectPath("IDPlugin/Sample/include/Sample.hpp")), "no copy deleted");

        FinishCase("id.project.l2", root, verdict);
    }

    /// L3：同一路径的 `kind` 在两代之间翻转 → record_invalid(2)
    void CaseL3() noexcept
    {
        const CaseRoot root("l3");
        CaseVerdict    verdict;

        verdict.Add(PrepareCommitted(verdict, root, "L3"), "committed baseline");

        std::string manifest_text;
        verdict.Add(ReadText(ProjectRecordPath(root, "manifest.json"), manifest_text), "manifest readable");
        // plugin_config.json 恒为 copied：把它改成 generated 制造 kind 翻转
        const std::string from = "\"kind\": \"copied\"";
        const std::string to   = "\"kind\": \"generated\"";
        verdict.Add(RewriteManifestAndSync(verdict, root, from, to), "flip kind of one copied entry");

        const CommandResult update = RunProject(root, { "project", "update", "L3" });
        AddExit(verdict, "update exit2", update, 2);
        CheckCategory(verdict, "record_invalid", update, "record_invalid");

        FinishCase("id.project.l3", root, verdict);
    }

    /// L4：无 `intent` 时手工删除一个已登记副本 → `build` 报 record_invalid(2)；`update` 重建
    void CaseL4() noexcept
    {
        const CaseRoot root("l4");
        CaseVerdict    verdict;

        verdict.Add(PrepareCommitted(verdict, root, "L4"), "committed baseline");

        const std::string copy_path = root.ProjectPath("IDPlugin/Sample/include/Sample.hpp");
        bool              is_digest_ok = false;
        const std::string digest_before = Sha256OfFile(copy_path, is_digest_ok);
        verdict.Add(RemoveTree(copy_path), "user deleted a copied file");
        verdict.Add(!IsRegularFile(copy_path), "copy absent");

        const CommandResult build = RunProject(root, { "project", "build", "L4", "--config", "debug" });
        AddExit(verdict, "build exit2", build, 2);
        CheckCategory(verdict, "build record_invalid", build, "record_invalid");

        const CommandResult update = RunProject(root, { "project", "update", "L4" });
        AddExit(verdict, "update exit0", update, 0);

        bool              is_rebuilt_ok = false;
        const std::string digest_after  = Sha256OfFile(copy_path, is_rebuilt_ok);
        verdict.Add(is_rebuilt_ok && digest_after == digest_before, "copy rebuilt with the recorded content");

        FinishCase("id.project.l4", root, verdict);
    }

    /// L5：手工删除 `src/main.cpp` 后 `build` 是 CMake 真实失败（4）且不报 record_invalid；`update` 重建
    void CaseL5() noexcept
    {
        const CaseRoot root("l5");
        CaseVerdict    verdict;

        verdict.Add(PrepareCommitted(verdict, root, "L5"), "committed baseline");

        const std::string main_path = root.ProjectPath("src/main.cpp");
        verdict.Add(RemoveTree(main_path), "user deleted the seed source");

        const CommandResult build = RunProject(root, { "project", "build", "L5", "--generator", "MinGW Makefiles",
                                                       "--toolchain", "mingw", "--arch", "x64", "--config", "debug" });
        AddExit(verdict, "build exit4", build, 4);
        verdict.Add(!HasCategory(build, "record_invalid"), "build does not report record_invalid");

        const CommandResult update = RunProject(root, { "project", "update", "L5" });
        AddExit(verdict, "update exit0", update, 0);
        verdict.Add(IsRegularFile(main_path), "seed source rebuilt");

        JsonReader manifest;
        verdict.Add(ReadManifest(root, manifest), "manifest readable");
        bool        has_seed   = false;
        std::string seed_digest;
        for (std::size_t index = 0; index < manifest.Count("entries"); ++index)
        {
            const std::string prefix = "entries." + std::to_string(index) + ".";
            if (manifest.String(prefix + "kind") == "seed")
            {
                has_seed    = true;
                seed_digest = manifest.String(prefix + "digest");
            }
        }
        bool              is_digest_ok = false;
        const std::string actual       = Sha256OfFile(main_path, is_digest_ok);
        verdict.Add(has_seed && is_digest_ok && seed_digest == actual, "seed entry digest equals rebuilt content");

        FinishCase("id.project.l5", root, verdict);
    }

    // ==================== 命名 ====================

    /// M1：插件名命中保留目标 `clean` / `install` → cmake_name_conflict(1)
    void CaseM1() noexcept
    {
        const CaseRoot root("m1");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("clean")), "fixture plugin named clean");
        const CommandResult first =
            RunProject(root, { "project", "new", "--name", "M1", "--path", root.project_root(), "--plugin", "clean" });
        AddExit(verdict, "clean exit1", first, 1);
        CheckCategory(verdict, "clean cmake_name_conflict", first, "cmake_name_conflict");

        verdict.Add(RemoveTree(root.EnginePath("IDPlugin/clean")), "remove clean fixture");
        verdict.Add(WritePlugin(root, SimplePlugin("install")), "fixture plugin named install");
        const CommandResult second =
            RunProject(root, { "project", "new", "--name", "M1", "--path", root.project_root(), "--plugin", "install" });
        AddExit(verdict, "install exit1", second, 1);
        CheckCategory(verdict, "install cmake_name_conflict", second, "cmake_name_conflict");

        FinishCase("id.project.m1", root, verdict);
    }

    /// M2：两个插件声明同名宏；选项宏取生成器保留名 → cmake_name_conflict(1)
    void CaseM2() noexcept
    {
        const CaseRoot root("m2");
        CaseVerdict    verdict;

        const std::string option =
            "[ { \"name\": \"FEATURE\", \"type\": \"bool\", \"default\": \"OFF\", \"description\": \"fixture\","
            " \"defines\": \"SAME_MACRO\" } ]";
        verdict.Add(WritePlugin(root, SimplePlugin("Alpha", "[]", option)), "fixture Alpha with SAME_MACRO");
        verdict.Add(WritePlugin(root, SimplePlugin("Beta", "[]", option)), "fixture Beta with SAME_MACRO");
        const CommandResult same_macro =
            RunProject(root, { "project", "new", "--name", "M2", "--path", root.project_root(), "--plugin", "Alpha",
                               "--plugin", "Beta" });
        AddExit(verdict, "same macro exit1", same_macro, 1);
        CheckCategory(verdict, "same macro cmake_name_conflict", same_macro, "cmake_name_conflict");
        verdict.Add(HasCategory(same_macro, "SAME_MACRO"), "diagnostic lists the conflicting macro");

        verdict.Add(RemoveTree(root.EnginePath("IDPlugin/Alpha")), "remove Alpha fixture");
        verdict.Add(RemoveTree(root.EnginePath("IDPlugin/Beta")), "remove Beta fixture");
        const std::string reserved_option =
            "[ { \"name\": \"ENABLE_LOG\", \"type\": \"bool\", \"default\": \"OFF\", \"description\": \"fixture\","
            " \"defines\": \"ID_DEBUG\" } ]";
        verdict.Add(WritePlugin(root, SimplePlugin("Gamma", "[]", reserved_option)), "fixture Gamma with reserved macro");
        const CommandResult reserved =
            RunProject(root, { "project", "new", "--name", "M2", "--path", root.project_root(), "--plugin", "Gamma" });
        AddExit(verdict, "reserved macro exit1", reserved, 1);
        CheckCategory(verdict, "reserved macro cmake_name_conflict", reserved, "cmake_name_conflict");

        FinishCase("id.project.m2", root, verdict);
    }

    /**
     *  M3：两个插件引用同一 vendor 目标 → 通过，聚合文件的 vendor 接入行只出现一次，构建成功
     *
     *  断言形态取自被测实现的实际生成文本（`ID/src/project/Emit/AggregateEmit.cpp`：源表达式
     *  `${CMAKE_CURRENT_SOURCE_DIR}/Vendor/<dir>`、二进制表达式 `${CMAKE_CURRENT_BINARY_DIR}/<dir>`、
     *  第三参数 `EXCLUDE_FROM_ALL`；`DirectoryEmitter.cpp` 在接入行前写 `if(TARGET <target>)` 守卫、
     *  在全部 `add_subdirectory` 之后写 `set_target_properties(<target> ...` 适配块）。
     */
    void CaseM3() noexcept
    {
        const CaseRoot root("m3");
        CaseVerdict    verdict;

        PluginSpec first  = SimplePlugin("Alpha");
        PluginSpec second = SimplePlugin("Beta");
        first.shared_vendors_json  = "[ { \"dir\": \"Vend\", \"target\": \"vend\" } ]";
        second.shared_vendors_json = "[ { \"dir\": \"Vend\", \"target\": \"vend\" } ]";
        verdict.Add(WritePlugin(root, first), "fixture Alpha shares vendor Vend");
        verdict.Add(WritePlugin(root, second), "fixture Beta shares vendor Vend");

        verdict.Add(WriteText(root.EnginePath("IDPlugin/Vendor/Vend/CMakeLists.txt"),
                              "cmake_minimum_required(VERSION 4.1.0)\n\nproject(vend)\n\n"
                              "add_library(vend INTERFACE)\n"),
                    "vendor CMake project fixture");
        verdict.Add(WriteText(root.EnginePath("IDPlugin/Vendor/Vend/VEND.md"), "vendor doc\n"), "vendor doc fixture");

        const CommandResult create =
            RunProject(root, { "project", "new", "--name", "M3", "--path", root.project_root(), "--plugin", "Alpha",
                               "--plugin", "Beta" });
        AddExit(verdict, "new exit0", create, 0);

        std::string project_plugins_cmake;
        ReadText(root.ProjectPath("IDPlugin/CMakeLists.txt"), project_plugins_cmake);

        // 接入行：两个表达式与 EXCLUDE_FROM_ALL 三者齐全，且恰好一次（重复接入会让三方目标重定义）
        const std::string access_line =
            "add_subdirectory(\"${CMAKE_CURRENT_SOURCE_DIR}/Vendor/Vend\" \"${CMAKE_CURRENT_BINARY_DIR}/Vend\" "
            "EXCLUDE_FROM_ALL)";
        verdict.Add(CountOccurrences(project_plugins_cmake, access_line) == 1,
                    "vendor access line (both expressions + EXCLUDE_FROM_ALL) appears exactly once");

        const std::string guard_text        = "if(TARGET vend)";
        const std::size_t guard_position    = project_plugins_cmake.find(guard_text);
        const std::size_t access_position   = project_plugins_cmake.find(access_line);
        verdict.Add(CountOccurrences(project_plugins_cmake, guard_text) == 1,
                    "vendor guard if(TARGET vend) appears exactly once");
        verdict.Add(guard_position != std::string::npos && access_position != std::string::npos
                        && guard_position < access_position,
                    "guard precedes the vendor access line");

        // 插件子目录：除接入行外的每条 add_subdirectory 都必须晚于守卫（守卫紧贴在被保护的接入之前）
        std::size_t plugin_subdir_count = 0;
        std::size_t first_subdir        = std::string::npos;
        std::size_t cursor              = 0;
        while ((cursor = project_plugins_cmake.find("add_subdirectory(", cursor)) != std::string::npos)
        {
            if (project_plugins_cmake.compare(cursor, access_line.size(), access_line) != 0)
            {
                ++plugin_subdir_count;
                if (first_subdir == std::string::npos)
                {
                    first_subdir = cursor;
                }
            }
            cursor += 1;
        }
        verdict.Add(plugin_subdir_count == 2, "two plugin add_subdirectory lines (Alpha, Beta)");
        verdict.Add(guard_position != std::string::npos && first_subdir != std::string::npos
                        && guard_position < first_subdir,
                    "guard precedes every plugin add_subdirectory");

        // 尾部适配块：`set_target_properties(vend ` 存在，且在全部 add_subdirectory 之后
        const std::string adaptation_prefix   = "set_target_properties(vend ";
        const std::size_t adaptation_position = project_plugins_cmake.find(adaptation_prefix);
        const std::size_t last_subdir         = project_plugins_cmake.rfind("add_subdirectory(");
        verdict.Add(adaptation_position != std::string::npos, "tail set_target_properties(vend ...) present");
        verdict.Add(adaptation_position != std::string::npos && last_subdir != std::string::npos
                        && adaptation_position > last_subdir,
                    "adaptation block comes after all add_subdirectory lines");
        if (CountOccurrences(project_plugins_cmake, access_line) != 1 || CountOccurrences(project_plugins_cmake, guard_text) != 1)
        {
            verdict.Note("IDPlugin/CMakeLists.txt", project_plugins_cmake.substr(0, 600));
        }

        std::vector<std::string> arguments = { "project", "build", "M3" };
        AppendMingwProfile(arguments);
        arguments.emplace_back("--config");
        arguments.emplace_back("debug");
        AddExit(verdict, "build exit0", RunProject(root, arguments), 0);

        FinishCase("id.project.m3", root, verdict);
    }

    // ==================== 并发 ====================

    /// N1：预置非本机 host 的活跃锁 → writer_conflict(2)，锁文件与项目内容零写
    void CaseN1() noexcept
    {
        const CaseRoot root("n1");
        CaseVerdict    verdict;

        verdict.Add(PrepareCommitted(verdict, root, "N1"), "committed baseline");

        const std::string lock_text =
            "pid=4242\nprocess_start=170000000000000000\nhost=fixture-other-host\ntaken_at=2026-01-01T00:00:00+00:00\n";
        const std::string lock_path = root.ProjectPath("IDGenerated/.lock");
        verdict.Add(WriteText(lock_path, lock_text), "preplaced active lock from another host");

        const CommandResult update = RunProject(root, { "project", "update", "N1" });
        AddExit(verdict, "update exit2", update, 2);
        CheckCategory(verdict, "writer_conflict", update, "writer_conflict");
        verdict.Add(HasCategory(update, "4242") && HasCategory(update, "fixture-other-host"),
                    "diagnostic carries pid/host/taken_at");

        std::string lock_after;
        verdict.Add(ReadText(lock_path, lock_after) && lock_after == lock_text, "lock file unchanged");

        FinishCase("id.project.n1", root, verdict);
    }

    /// N2：登记已含另一项目，在同一假引擎根上 `new` → 两条登记都在，旧条目逐字不变
    void CaseN2() noexcept
    {
        const CaseRoot root("n2");
        CaseVerdict    verdict;

        const std::string other_root = root.CasePath("other-project");
        verdict.Add(CreateDirectories(other_root), "other project fixture");
        const std::string registry_before =
            "[ { \"name\": \"Other\", \"path\": \"" + other_root + "\", \"is_initialized\": true } ]";
        verdict.Add(WriteText(RegistryPath(root), registry_before), "registry with another project");

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        AddExit(verdict, "new exit0",
                RunProject(root, { "project", "new", "--name", "N2", "--path", root.project_root(), "--plugin", "Sample" }),
                0);

        std::string registry_after;
        verdict.Add(ReadText(RegistryPath(root), registry_after), "registry readable");
        verdict.Add(registry_after.find("Other") != std::string::npos, "old entry still present");
        verdict.Add(registry_after.find("N2") != std::string::npos, "new entry present");

        JsonReader registry;
        verdict.Add(registry.LoadText(registry_after) && registry.Count("") == 2, "two registry entries");

        const CommandResult list = RunProject(root, { "project", "list" });
        AddExit(verdict, "list exit0", list, 0);
        verdict.Add(HasCategory(list, "Other") && HasCategory(list, "N2"), "list shows both projects");

        FinishCase("id.project.n2", root, verdict);
    }

    /// N3：`build` 期间跑 `--check` → snapshot_unstable(2)；无窗口注入点，登记 Uncovered
    void CaseN3() noexcept
    {
        ID::Bench::Uncovered(
            "id.project.n3",
            "§11.6 U-1：只读快照的读取窗口由实现内部时序决定，没有窗口注入点；靠重试碰窗口不算取证。"
            "目标期望（补点位后转常规用例）为 snapshot_unstable(2)。"
            "实现侧已补齐 `--check` 复验（`Plan/Snapshot.cpp`，读取前后逐项比对、不一致时报 snapshot_unstable），"
            "本用例仅因无法稳定注入窗口而不可取证，不写成通过；正常输入下复验通过的证据见 "
            "`id.project.d7`（`update --check` / `clean --check` 两项子条件）");
    }

    /// N4：伪造陈旧锁（本机 host + 已退出进程号）→ warn 回收并继续，update 0
    void CaseN4() noexcept
    {
        const CaseRoot root("n4");
        CaseVerdict    verdict;

        verdict.Add(PrepareCommitted(verdict, root, "N4"), "committed baseline");

        // 取本机 host：与实现同一来源（环境变量 COMPUTERNAME）
        const char* const host_name = std::getenv("COMPUTERNAME");
        const std::string host      = host_name != nullptr ? std::string(host_name) : std::string("unknown-host");

        // 一个已退出的进程号：本进程的 pid 减一不保证存在，但更稳的是用 0xFFFFFF 这样的不存在进程号
        const std::string lock_text = "pid=4294967295\nprocess_start=1\nhost=" + host
                                    + "\ntaken_at=2026-01-01T00:00:00+00:00\n";
        const std::string lock_path = root.ProjectPath("IDGenerated/.lock");
        verdict.Add(WriteText(lock_path, lock_text), "preplaced stale lock with a dead pid");

        const CommandResult update = RunProject(root, { "project", "update", "N4" });
        AddExit(verdict, "update exit0", update, 0);
        verdict.Add(!HasCategory(update, "writer_conflict"), "stale lock recycled without writer_conflict");

        std::string lock_after;
        const bool  is_lock_present = ReadText(lock_path, lock_after);
        if (is_lock_present)
        {
            verdict.Note("lock_after", lock_after.substr(0, 80));
        }
        else
        {
            verdict.Note("lock_after", "<absent after release>");
        }

        FinishCase("id.project.n4", root, verdict);
    }
} // namespace

namespace ID::Bench
{
    void IdProjectCasesJKLMN() noexcept
    {
        CaseJ1();
        CaseJ2();
        CaseJ3();
        CaseK1();
        CaseK2();
        CaseK3();
        CaseK4();
        CaseK5();
        CaseK6();
        CaseL1();
        CaseL2();
        CaseL3();
        CaseL4();
        CaseL5();
        CaseM1();
        CaseM2();
        CaseM3();
        CaseN1();
        CaseN2();
        CaseN3();
        CaseN4();
    }
} // namespace ID::Bench
