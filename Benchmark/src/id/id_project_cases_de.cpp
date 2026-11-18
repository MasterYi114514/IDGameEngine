#include <algorithm>
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
 *  `id.project` 无写组（D1–D7）与用户保护组（E1–E7）。
 *
 *  判定口径（`docs/id/11-verification-matrix.md` §11.4）：
 *  - "零写"用 `SnapshotFixture` 口径的快照判定：项目根全树 + 假引擎根 `ID/` 的目录集合、文件集合、
 *    逐文件 SHA-256 与 mtime 全部相同；锁文件、日志、`*.idtmp.*` 都会以新增条目出现。
 *  - `--check` 的动作集合与真实执行的动作集合按"去掉 `#fragment` 后的路径集合"比较（真实执行行不带
 *    片段名，见 `probe3` 实测格式；`unchanged` 行不计入集合）。
 *  - 用户保护类断言只看"用户字节是否被改写/删除"，不把工具的备份位置写成用户文件。
 */

namespace
{
    using ID::Bench::Check;
    using ID::Bench::IdProject::AddExit;
    using ID::Bench::IdProject::ArtifactPath;
    using ID::Bench::IdProject::CaseRoot;
    using ID::Bench::IdProject::CaseVerdict;
    using ID::Bench::IdProject::CheckCategory;
    using ID::Bench::IdProject::CommandResult;
    using ID::Bench::IdProject::CountTreeEntries;
    using ID::Bench::IdProject::HasCategory;
    using ID::Bench::IdProject::IsDirectory;
    using ID::Bench::IdProject::IsRegularFile;
    using ID::Bench::IdProject::JsonReader;
    using ID::Bench::IdProject::ListTree;
    using ID::Bench::IdProject::PluginSpec;
    using ID::Bench::IdProject::ProjectRecordPath;
    using ID::Bench::IdProject::ReadManifest;
    using ID::Bench::IdProject::ReadResolved;
    using ID::Bench::IdProject::ReadText;
    using ID::Bench::IdProject::RunProject;
    using ID::Bench::IdProject::Sha256Hex;
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

    /// @brief 按行前缀取路径集合（去掉 `#fragment` 段与首尾空白）
    std::vector<std::string> ExtractPaths(const std::string& text, std::string_view prefix) noexcept
    {
        std::vector<std::string> paths;
        std::size_t              begin = 0;
        while (begin < text.size())
        {
            const std::size_t end  = text.find('\n', begin);
            const std::size_t stop = (end == std::string::npos) ? text.size() : end;

            std::string line = text.substr(begin, stop - begin);
            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            {
                line.pop_back();
            }
            std::size_t first = 0;
            while (first < line.size() && line[first] == ' ')
            {
                ++first;
            }
            if (line.compare(first, prefix.size(), prefix) == 0)
            {
                std::string value = line.substr(first + prefix.size());
                const std::size_t hash = value.find('#');
                if (hash != std::string::npos)
                {
                    value = value.substr(0, hash);
                }
                if (!value.empty())
                {
                    paths.push_back(value);
                }
            }

            if (end == std::string::npos)
            {
                break;
            }
            begin = end + 1;
        }

        std::sort(paths.begin(), paths.end());
        paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
        return paths;
    }

    /// @brief 快照里是否存在以某后缀结尾的条目
    bool HasSnapshotSuffix(const ID::Bench::IdProject::Snapshot& snapshot, std::string_view suffix) noexcept
    {
        for (const std::string& entry : snapshot.entries)
        {
            if (entry.size() >= suffix.size() && entry.compare(entry.size() - suffix.size(), suffix.size(), suffix) == 0)
            {
                return true;
            }
        }
        return false;
    }

    // ==================== 无写 ====================

    /// D1：`list` / `new --check` / `update --check` / `clean --check` 前后快照一致，无锁无日志无临时文件
    void CaseD1() noexcept
    {
        const CaseRoot root("d1");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        const CommandResult create =
            RunProject(root, {"project", "new", "--name", "D1", "--path", root.project_root(), "--plugin", "Sample"});
        AddExit(verdict, "baseline new exit0", create, 0);

        const std::vector<std::vector<std::string>> commands = {
            { "project", "list" },
            { "project", "new", "--name", "D1", "--path", root.project_root(), "--check" },
            { "project", "update", "D1", "--check" },
            { "project", "clean", "D1", "--check" },
        };

        for (const std::vector<std::string>& command : commands)
        {
            const ID::Bench::IdProject::Snapshot before = TakeSnapshot(root);
            const CommandResult result                   = RunProject(root, command);
            const ID::Bench::IdProject::Snapshot after = TakeSnapshot(root);

            const std::string diff = SnapshotDiff(before, after);
            verdict.Add(diff.empty(), std::string("zero write: ") + command[1]);
            if (!diff.empty())
            {
                verdict.Note("diff", diff.substr(0, 400));
            }
            verdict.Add(!HasSnapshotSuffix(after, ".lock"), std::string("no lock file: ") + command[1]);
            verdict.Add(!HasSnapshotSuffix(after, ".idtmp.json"), std::string("no temp file: ") + command[1]);
            if (command[1] == "list")
            {
                AddExit(verdict, "list exit0", result, 0);
            }
        }

        FinishCase("id.project.d1", root, verdict);
    }

    /// D2：`--check` 的动作集合与真实执行的动作集合一致（unchanged 不计入）
    void CaseD2() noexcept
    {
        const CaseRoot root("d2");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        const CommandResult create =
            RunProject(root, {"project", "new", "--name", "D2", "--path", root.project_root(), "--empty"});
        AddExit(verdict, "baseline new exit0", create, 0);

        // 引入变化：把直接选择从「无」改成 Sample
        verdict.Add(WritePluginsJson(root, "D2",
                                     "[ { \"name\": \"Sample\", \"linkage_type\": \"static\", \"options\": {} } ]"),
                    "user adds a direct selection");

        const CommandResult check = RunProject(root, {"project", "update", "D2", "--check"});
        verdict.Add(check.exit_code == 6, "check exit6 for planned changes");

        const std::vector<std::string> planned_writes = ExtractPaths(check.out_text, "将写入 ");
        const std::vector<std::string> planned_deletes = ExtractPaths(check.out_text, "将删除 ");
        verdict.Add(!planned_writes.empty(), "check lists planned writes");

        const CommandResult real = RunProject(root, {"project", "update", "D2"});
        AddExit(verdict, "real update exit0", real, 0);

        std::vector<std::string> real_writes = ExtractPaths(real.out_text, "written ");
        const std::vector<std::string> real_deletes = ExtractPaths(real.out_text, "removed ");
        std::sort(real_writes.begin(), real_writes.end());
        real_writes.erase(std::unique(real_writes.begin(), real_writes.end()), real_writes.end());

        verdict.Add(planned_writes == real_writes, "planned writes == real written set");
        verdict.Add(planned_deletes == real_deletes, "planned deletes == real removed set");
        if (planned_writes != real_writes)
        {
            verdict.Note("planned", std::to_string(planned_writes.size()));
            verdict.Note("real", std::to_string(real_writes.size()));
        }

        FinishCase("id.project.d2", root, verdict);
    }

    /// D3：新增选择 + 手改受管副本 → update --check 报 unmanaged_content(2)，两组路径都列出，零写
    void CaseD3() noexcept
    {
        const CaseRoot root("d3");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        verdict.Add(WritePlugin(root, SimplePlugin("Extra")), "fixture plugin Extra");
        const CommandResult create =
            RunProject(root, {"project", "new", "--name", "D3", "--path", root.project_root(), "--plugin", "Sample"});
        AddExit(verdict, "baseline new exit0", create, 0);

        const std::string copy_path = root.ProjectPath("IDPlugin/Sample/include/Sample.hpp");
        std::string       copy_text;
        verdict.Add(ReadText(copy_path, copy_text), "managed copy readable");
        verdict.Add(WriteText(copy_path, copy_text + "// local modification\n"), "user modified managed copy");
        verdict.Add(WritePluginsJson(root, "D3",
                                     "[ { \"name\": \"Sample\", \"linkage_type\": \"static\", \"options\": {} },\n"
                                     "  { \"name\": \"Extra\", \"linkage_type\": \"static\", \"options\": {} } ]"),
                    "user adds Extra selection");

        const ID::Bench::IdProject::Snapshot before = TakeSnapshot(root);
        const CommandResult check = RunProject(root, {"project", "update", "D3", "--check"});
        const ID::Bench::IdProject::Snapshot after = TakeSnapshot(root);

        AddExit(verdict, "check exit2", check, 2);
        CheckCategory(verdict, "unmanaged_content", check, "unmanaged_content");
        verdict.Add(HasCategory(check, "IDPlugin/Sample/include/Sample.hpp"), "conflict path listed");
        verdict.Add(HasCategory(check, "IDPlugin/Extra/plugin_config.json"), "planned write path listed");
        verdict.Add(SnapshotDiff(before, after).empty(), "zero write");
        verdict.Add(check.exit_code != 6, "does not return 6");

        FinishCase("id.project.d3", root, verdict);
    }

    /**
     *  D4：同一夹具（未登记路径 + 写入冲突）按命令给类别——`update --check` 取 2、`clean --check` 取 5
     *
     *  口径（已裁决）：`new`/`update` 的写入冲突与保留集内手改副本归 `unmanaged_content`（2）；
     *  `clean` 的一切裁剪冲突（摘要不符、未登记路径）归 `cleanup_blocked`（5）。一个类别只有一个码，
     *  同一语义不因阶段不同换类别（spec §13.1 末条、§8.8）。两次运行都零写、输出逐字节稳定。
     */
    void CaseD4() noexcept
    {
        const CaseRoot root("d4");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        const CommandResult create =
            RunProject(root, {"project", "new", "--name", "D4", "--path", root.project_root(), "--plugin", "Sample"});
        AddExit(verdict, "baseline new exit0", create, 0);

        const std::string copy_path = root.ProjectPath("IDPlugin/Sample/include/Sample.hpp");
        std::string       copy_text;
        ReadText(copy_path, copy_text);
        verdict.Add(WriteText(copy_path, copy_text + "// local modification\n"), "write conflict fixture");
        verdict.Add(WriteText(root.ProjectPath("IDPlugin/UNREGISTERED.txt"), "user file\n"), "unregistered path fixture");

        // update --check：写入冲突归 unmanaged_content（2）
        const ID::Bench::IdProject::Snapshot update_before = TakeSnapshot(root);
        const CommandResult update_first                   = RunProject(root, {"project", "update", "D4", "--check"});
        const CommandResult update_second                  = RunProject(root, {"project", "update", "D4", "--check"});
        const ID::Bench::IdProject::Snapshot update_after  = TakeSnapshot(root);

        AddExit(verdict, "update --check exit2", update_first, 2);
        CheckCategory(verdict, "update conflict category", update_first, "unmanaged_content");
        verdict.Add(update_first.exit_code != 6, "update --check does not return 6");
        verdict.Add(update_first.out_text == update_second.out_text, "update --check output is byte-stable");
        verdict.Add(SnapshotDiff(update_before, update_after).empty(), "update --check zero write");
        verdict.Add(HasCategory(update_first, "IDPlugin/Sample/include/Sample.hpp"), "update lists the conflict path");

        // clean --check：一切裁剪冲突归 cleanup_blocked（5）
        const ID::Bench::IdProject::Snapshot clean_before = TakeSnapshot(root);
        const CommandResult clean_first                   = RunProject(root, {"project", "clean", "D4", "--check"});
        const CommandResult clean_second                  = RunProject(root, {"project", "clean", "D4", "--check"});
        const ID::Bench::IdProject::Snapshot clean_after  = TakeSnapshot(root);

        AddExit(verdict, "clean --check exit5", clean_first, 5);
        CheckCategory(verdict, "clean trim conflict category", clean_first, "cleanup_blocked");
        verdict.Add(clean_first.exit_code != 6, "clean --check does not return 6");
        verdict.Add(clean_first.out_text == clean_second.out_text, "clean --check output is byte-stable");
        verdict.Add(SnapshotDiff(clean_before, clean_after).empty(), "clean --check zero write");
        verdict.Add(HasCategory(clean_first, "IDPlugin/UNREGISTERED.txt"), "clean lists the unregistered path");
        verdict.Add(HasCategory(clean_first, "IDPlugin/Sample/include/Sample.hpp"), "clean lists the conflicting copy");

        FinishCase("id.project.d4", root, verdict);
    }

    /// D5：已一致项目三类 `--check` 皆空 → 0；只有变更 → 6；都零写
    void CaseD5() noexcept
    {
        const CaseRoot root("d5");
        CaseVerdict    verdict;

        PluginSpec spec = SimplePlugin(
            "Sample", "[]",
            "[ { \"name\": \"ENABLE_LOG\", \"type\": \"bool\", \"default\": \"OFF\", \"description\": \"fixture\" } ]");
        verdict.Add(WritePlugin(root, spec), "fixture plugin Sample with option");
        const CommandResult create =
            RunProject(root, {"project", "new", "--name", "D5", "--path", root.project_root(), "--plugin", "Sample"});
        AddExit(verdict, "baseline new exit0", create, 0);

        for (const char* const command : { "new", "update", "clean" })
        {
            const ID::Bench::IdProject::Snapshot before = TakeSnapshot(root);
            std::vector<std::string>                arguments = { "project", command, "D5", "--check" };
            if (std::string_view(command) == "new")
            {
                arguments = { "project", "new", "--name", "D5", "--path", root.project_root(), "--check" };
            }
            const CommandResult result = RunProject(root, arguments);
            const ID::Bench::IdProject::Snapshot after = TakeSnapshot(root);

            AddExit(verdict, std::string(command) + " --check exit0 (no changes)", result, 0);
            verdict.Add(SnapshotDiff(before, after).empty(), std::string(command) + " --check zero write");
        }

        // 另一夹具：只改 plugins.json 增加一个选项取值
        verdict.Add(WritePluginsJson(root, "D5",
                                     "[ { \"name\": \"Sample\", \"linkage_type\": \"static\","
                                     " \"options\": { \"ENABLE_LOG\": \"ON\" } } ]"),
                    "only plugins.json changed");
        const ID::Bench::IdProject::Snapshot before = TakeSnapshot(root);
        const CommandResult check = RunProject(root, {"project", "update", "D5", "--check"});
        const ID::Bench::IdProject::Snapshot after = TakeSnapshot(root);
        AddExit(verdict, "only-change check exit6", check, 6);
        verdict.Add(SnapshotDiff(before, after).empty(), "only-change check zero write");

        FinishCase("id.project.d5", root, verdict);
    }

    /// D6：手工把 is_initialized 改成 false 后 update --check → 零写、列出将重写标记、退出 6
    void CaseD6() noexcept
    {
        const CaseRoot root("d6");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        const CommandResult create =
            RunProject(root, {"project", "new", "--name", "D6", "--path", root.project_root(), "--plugin", "Sample"});
        AddExit(verdict, "baseline new exit0", create, 0);

        const std::string registry_path = ID::Bench::IdProject::RegistryPath(root);
        const std::string registry      = "[ { \"name\": \"D6\", \"path\": \"" + root.project_root()
                                   + "\", \"is_initialized\": false } ]";
        verdict.Add(WriteText(registry_path, registry), "user flipped is_initialized to false");

        std::string registry_before;
        verdict.Add(ReadText(registry_path, registry_before), "registry readable");

        const CommandResult check = RunProject(root, {"project", "update", "D6", "--check"});

        std::string registry_after;
        verdict.Add(ReadText(registry_path, registry_after) && registry_after == registry_before,
                    "registry bytes unchanged (zero write)");
        AddExit(verdict, "check exit6", check, 6);
        verdict.Add(HasCategory(check, "将重写 is_initialized 派生缓存"), "plan lists marker rewrite");

        FinishCase("id.project.d6", root, verdict);
    }

    /**
     *  D7：只读快照复验（spec §16 第 17 条、§10.12）在**正常输入**下不掉档。
     *
     *  `update --check` / `clean --check` 输出前的状态复验必须通过，因此：
     *  - 退出码仍是计划语义的 0（三类皆空）或 6（只有变更），不出现 `snapshot_unstable`（2）；
     *  - 零写（项目根全树 + 假引擎根 `ID/` 的快照逐条相同）；
     *  - 同一夹具连跑两次的输出逐字节相同（复验本身不引入随机/时序噪声）。
     *
     *  这是稳定构造（无并发、无时序依赖）：`snapshot_unstable` 只在读取窗口内被改写时出现，
     *  该窗口无法受控注入，按 `id.project.n3` 登记 `Uncovered`，不靠重试碰窗口取证。
     */
    void CaseD7() noexcept
    {
        const CaseRoot root("d7");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        const CommandResult create =
            RunProject(root, {"project", "new", "--name", "D7", "--path", root.project_root(), "--plugin", "Sample"});
        AddExit(verdict, "baseline new exit0", create, 0);

        for (const char* const command : { "update", "clean" })
        {
            const std::string_view   action             = command;
            const ID::Bench::IdProject::Snapshot before = TakeSnapshot(root);
            const CommandResult first                   = RunProject(root, {"project", command, "D7", "--check"});
            const CommandResult second                  = RunProject(root, {"project", command, "D7", "--check"});
            const ID::Bench::IdProject::Snapshot after  = TakeSnapshot(root);

            verdict.Add(first.exit_code == 0 || first.exit_code == 6,
                        std::string(action) + " --check exit 0 or 6 (no snapshot_unstable)");
            verdict.Add(!HasCategory(first, "snapshot_unstable"),
                        std::string(action) + " --check reports no snapshot_unstable");
            verdict.Add(first.out_text == second.out_text,
                        std::string(action) + " --check output is byte-stable");
            verdict.Add(SnapshotDiff(before, after).empty(), std::string(action) + " --check zero write");
        }

        FinishCase("id.project.d7", root, verdict);
    }

    // ==================== 用户保护 ====================

    /// E1：手改受管副本 → unmanaged_content(2)，列出路径，不覆盖
    void CaseE1() noexcept
    {
        const CaseRoot root("e1");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        const CommandResult create =
            RunProject(root, {"project", "new", "--name", "E1", "--path", root.project_root(), "--plugin", "Sample"});
        AddExit(verdict, "baseline new exit0", create, 0);

        const std::string copy_path = root.ProjectPath("IDPlugin/Sample/include/Sample.hpp");
        std::string       copy_text;
        verdict.Add(ReadText(copy_path, copy_text), "managed copy readable");
        const std::string modified = copy_text + "// local one byte change\n";
        verdict.Add(WriteText(copy_path, modified), "user modified managed copy");

        const CommandResult update = RunProject(root, {"project", "update", "E1"});
        AddExit(verdict, "update exit2", update, 2);
        CheckCategory(verdict, "unmanaged_content", update, "unmanaged_content");
        verdict.Add(HasCategory(update, "IDPlugin/Sample/include/Sample.hpp"), "conflict path listed");

        std::string after;
        verdict.Add(ReadText(copy_path, after) && after == modified, "file not overwritten");

        FinishCase("id.project.e1", root, verdict);
    }

    /// E2：E1 之后 `update --force` → 备份手改字节、覆盖为工具内容、退出 0
    void CaseE2() noexcept
    {
        const CaseRoot root("e2");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        const CommandResult create =
            RunProject(root, {"project", "new", "--name", "E2", "--path", root.project_root(), "--plugin", "Sample"});
        AddExit(verdict, "baseline new exit0", create, 0);

        const std::string copy_path = root.ProjectPath("IDPlugin/Sample/include/Sample.hpp");
        std::string       copy_text;
        ReadText(copy_path, copy_text);
        const std::string modified = copy_text + "// local one byte change\n";
        verdict.Add(WriteText(copy_path, modified), "user modified managed copy");
        const std::string modified_digest = Sha256Hex(modified);

        const CommandResult forced = RunProject(root, {"project", "update", "E2", "--force"});
        AddExit(verdict, "update --force exit0", forced, 0);

        // 备份目录 `<generation>/`：这里只断言存在一份内容等于手改后字节的副本
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
        verdict.Add(is_backup_found, "backup contains the abandoned local modification");

        std::string after;
        verdict.Add(ReadText(copy_path, after) && Sha256Hex(after) != modified_digest, "file overwritten with tool content");

        FinishCase("id.project.e2", root, verdict);
    }

    /// E3：`IDPlugin/` 放未登记文件 → update 2 / clean 5 / clean --force 仍 5，文件原样保留
    void CaseE3() noexcept
    {
        const CaseRoot root("e3");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        const CommandResult create =
            RunProject(root, {"project", "new", "--name", "E3", "--path", root.project_root(), "--plugin", "Sample"});
        AddExit(verdict, "baseline new exit0", create, 0);

        const std::string user_path = root.ProjectPath("IDPlugin/USER-NOTE.txt");
        verdict.Add(WriteText(user_path, "user file\n"), "user placed an unregistered file");
        const std::string user_digest = Sha256Hex("user file\n");

        const CommandResult update  = RunProject(root, {"project", "update", "E3"});
        const CommandResult clean   = RunProject(root, {"project", "clean", "E3"});
        const CommandResult forced  = RunProject(root, {"project", "clean", "E3", "--force"});

        AddExit(verdict, "update exit2", update, 2);
        CheckCategory(verdict, "update unmanaged_content", update, "unmanaged_content");
        AddExit(verdict, "clean exit5", clean, 5);
        CheckCategory(verdict, "clean cleanup_blocked", clean, "cleanup_blocked");
        AddExit(verdict, "clean --force still exit5", forced, 5);

        std::string after;
        verdict.Add(ReadText(user_path, after) && Sha256Hex(after) == user_digest,
                    "unregistered file kept with identical bytes");

        FinishCase("id.project.e3", root, verdict);
    }

    /// E4：手改 `src/main.cpp` 与根 `CMakeLists.txt` 片段外内容 → update 不覆盖
    void CaseE4() noexcept
    {
        const CaseRoot root("e4");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        const CommandResult create =
            RunProject(root, {"project", "new", "--name", "E4", "--path", root.project_root(), "--plugin", "Sample"});
        AddExit(verdict, "baseline new exit0", create, 0);

        const std::string main_path  = root.ProjectPath("src/main.cpp");
        const std::string cmake_path = root.ProjectPath("CMakeLists.txt");
        std::string       main_text;
        std::string       cmake_text;
        verdict.Add(ReadText(main_path, main_text) && ReadText(cmake_path, cmake_text), "user files readable");

        // 片段外内容：在文件末尾追加一行（两个片段都在其之前）
        const std::string main_modified  = main_text + "// user tail\n";
        const std::string cmake_modified = cmake_text + "# user tail outside fragments\n";
        verdict.Add(WriteText(main_path, main_modified), "user edited src/main.cpp");
        verdict.Add(WriteText(cmake_path, cmake_modified), "user edited root CMakeLists outside fragments");

        const CommandResult update = RunProject(root, {"project", "update", "E4"});
        AddExit(verdict, "update exit0", update, 0);

        std::string main_after;
        std::string cmake_after;
        verdict.Add(ReadText(main_path, main_after) && main_after == main_modified, "src/main.cpp kept");
        verdict.Add(ReadText(cmake_path, cmake_after)
                        && cmake_after.find("# user tail outside fragments") != std::string::npos,
                    "root CMakeLists outside bytes kept");

        FinishCase("id.project.e4", root, verdict);
    }

    /// E5：删掉一个 END 标记 / 把 END 标记重复 → 各自 unmanaged_content(2)，不整文件重建
    void CaseE5() noexcept
    {
        const CaseRoot root("e5");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        const CommandResult create =
            RunProject(root, {"project", "new", "--name", "E5", "--path", root.project_root(), "--plugin", "Sample"});
        AddExit(verdict, "baseline new exit0", create, 0);

        std::string cmake_text;
        verdict.Add(ReadText(root.ProjectPath("CMakeLists.txt"), cmake_text), "root CMakeLists readable");
        const std::string end_marker = "# <<< ID GENERATED END output-dirs\n";
        verdict.Add(cmake_text.find(end_marker) != std::string::npos, "END marker present in baseline");

        // 样本一：删掉 END 标记
        const std::string deleted = cmake_text;
        std::string       broken  = deleted;
        const std::size_t position = broken.find(end_marker);
        if (position != std::string::npos)
        {
            broken.erase(position, end_marker.size());
        }
        verdict.Add(WriteText(root.ProjectPath("CMakeLists.txt"), broken), "sample1: removed END marker");
        const CommandResult first = RunProject(root, {"project", "update", "E5"});
        AddExit(verdict, "removed marker update exit2", first, 2);
        CheckCategory(verdict, "removed marker unmanaged_content", first, "unmanaged_content");
        std::string after_first;
        verdict.Add(ReadText(root.ProjectPath("CMakeLists.txt"), after_first) && after_first == broken,
                    "file not rebuilt (bytes equal the broken content)");

        // 样本二：把 END 标记重复
        std::string duplicated = cmake_text;
        const std::size_t second_position = duplicated.find(end_marker);
        if (second_position != std::string::npos)
        {
            duplicated.insert(second_position, end_marker);
        }
        verdict.Add(WriteText(root.ProjectPath("CMakeLists.txt"), duplicated), "sample2: duplicated END marker");
        const CommandResult second = RunProject(root, {"project", "update", "E5"});
        AddExit(verdict, "duplicated marker update exit2", second, 2);
        CheckCategory(verdict, "duplicated marker unmanaged_content", second, "unmanaged_content");
        std::string after_second;
        verdict.Add(ReadText(root.ProjectPath("CMakeLists.txt"), after_second) && after_second == duplicated,
                    "duplicated file not rebuilt");

        FinishCase("id.project.e5", root, verdict);
    }

    /**
     *  E6：`shared_vendors` 中 `target: null` 的条目只复制、不接入构建，构建仍成功
     *
     *  否定断言与 M3 的阳性断言用同一形态（源表达式 `${CMAKE_CURRENT_SOURCE_DIR}/Vendor/<dir>`、
     *  二进制表达式、`EXCLUDE_FROM_ALL` 与尾部 `set_target_properties(<target> ` 适配块），
     *  并各带一条阳性对照，避免"读到空文件"让否定恒真。
     */
    void CaseE6() noexcept
    {
        const CaseRoot root("e6");
        CaseVerdict    verdict;

        // 共享 vendor：`Vendor/Vend/` 只有头与一个标记文件，不提供 CMake 目标
        PluginSpec spec = SimplePlugin("Sample");
        spec.shared_vendors_json = "[ { \"dir\": \"Vend\", \"target\": null } ]";
        verdict.Add(WritePlugin(root, spec), "fixture plugin with target:null shared vendor");
        verdict.Add(WriteText(root.EnginePath("IDPlugin/Vendor/Vend/vend.hpp"), "#pragma once\n"), "vendor header fixture");
        verdict.Add(WriteText(root.EnginePath("IDPlugin/Vendor/Vend/VEND.md"), "vendor readme\n"), "vendor doc fixture");

        const CommandResult create =
            RunProject(root, {"project", "new", "--name", "E6", "--path", root.project_root(), "--plugin", "Sample"});
        AddExit(verdict, "new exit0", create, 0);

        verdict.Add(IsRegularFile(root.ProjectPath("IDPlugin/Vendor/Vend/vend.hpp")), "vendor tree copied");

        std::string project_plugins_cmake;
        std::string plugin_cmake;
        ReadText(root.ProjectPath("IDPlugin/CMakeLists.txt"), project_plugins_cmake);
        ReadText(root.ProjectPath("IDPlugin/Sample/CMakeLists.txt"), plugin_cmake);

        // 阳性对照：聚合文件确实按已知形态生成了插件接入行，否则下面的否定断言会因为读到空文件而恒真
        verdict.Add(project_plugins_cmake.find("add_subdirectory(Sample)") != std::string::npos,
                    "aggregate file has add_subdirectory(Sample) (non-vacuous control)");

        // 否定：与 M3 同一形态的 vendor 接入行不得出现（两个表达式 + EXCLUDE_FROM_ALL 的整行）
        verdict.Add(project_plugins_cmake.find("add_subdirectory(\"${CMAKE_CURRENT_SOURCE_DIR}/Vendor/Vend\"")
                        == std::string::npos,
                    "no add_subdirectory access line for the target:null dir");
        verdict.Add(project_plugins_cmake.find("EXCLUDE_FROM_ALL") == std::string::npos,
                    "no EXCLUDE_FROM_ALL vendor wiring in the aggregate file");
        verdict.Add(project_plugins_cmake.find("set_target_properties(vend ") == std::string::npos,
                    "no vendor adaptation block for the target:null dir");

        // 插件文件：链接项里不得出现该目录的条目（`target_link_libraries` 块内检索目录名与目标名）
        const std::size_t link_position = plugin_cmake.find("target_link_libraries(Sample");
        const std::size_t link_end =
            link_position == std::string::npos ? std::string::npos : plugin_cmake.find(')', link_position);
        const std::string link_block =
            link_end == std::string::npos ? std::string() : plugin_cmake.substr(link_position, link_end - link_position + 1);
        verdict.Add(!link_block.empty(), "plugin CMakeLists has target_link_libraries(Sample ...) (non-vacuous control)");
        verdict.Add(!link_block.empty() && link_block.find("Vend") == std::string::npos,
                    "plugin link block lists no item from the target:null dir");
        verdict.Add(plugin_cmake.find("vend") == std::string::npos, "plugin CMakeLists names no vendor target");

        std::vector<std::string> arguments = { "project", "build", "E6" };
        ID::Bench::IdProject::AppendMingwProfile(arguments);
        arguments.emplace_back("--config");
        arguments.emplace_back("debug");
        const CommandResult build = RunProject(root, arguments);
        AddExit(verdict, "build exit0 with target:null vendor", build, 0);
        verdict.Add(IsRegularFile(ArtifactPath(root, "mingw", "x64", "debug", "E6.exe")), "artifact exists");

        FinishCase("id.project.e6", root, verdict);
    }

    /// E7：两个受控片段独立成条；restore 只撤销片段范围
    void CaseE7() noexcept
    {
        const CaseRoot root("e7");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        verdict.Add(WritePlugin(root, SimplePlugin("Extra")), "fixture plugin Extra");
        const CommandResult create =
            RunProject(root, {"project", "new", "--name", "E7", "--path", root.project_root(), "--plugin", "Sample"});
        AddExit(verdict, "baseline new exit0", create, 0);

        // ① 只改直接选择集合（影响 plugin-targets 片段），同时手改片段外内容
        std::string cmake_text;
        verdict.Add(ReadText(root.ProjectPath("CMakeLists.txt"), cmake_text), "root CMakeLists readable");
        const std::string outside = cmake_text + "# user tail outside fragments\n";
        verdict.Add(WriteText(root.ProjectPath("CMakeLists.txt"), outside), "user edited outside fragments");
        verdict.Add(WritePluginsJson(root, "E7",
                                     "[ { \"name\": \"Sample\", \"linkage_type\": \"static\", \"options\": {} },\n"
                                     "  { \"name\": \"Extra\", \"linkage_type\": \"static\", \"options\": {} } ]"),
                    "direct selection changed");

        const CommandResult update = RunProject(root, {"project", "update", "E7"});
        AddExit(verdict, "update exit0", update, 0);

        // 两个片段的 before-payload：本次 attempt 在收尾时删除，因此这里只看 manifest 两条片段条目
        JsonReader manifest;
        verdict.Add(ReadManifest(root, manifest), "manifest readable");
        std::size_t fragment_entries = 0;
        const std::size_t entry_count = manifest.Count("entries");
        for (std::size_t index = 0; index < entry_count; ++index)
        {
            const std::string prefix = "entries." + std::to_string(index) + ".";
            if (manifest.String(prefix + "path") == "CMakeLists.txt")
            {
                const ID::Json::JsonValue* fragment = manifest.Find(prefix + "fragment");
                if (fragment != nullptr && fragment->is_string() && !fragment->as_string().empty())
                {
                    ++fragment_entries;
                }
            }
        }
        verdict.Add(fragment_entries == 2, "two independent fragment entries in manifest");

        std::string cmake_after;
        verdict.Add(ReadText(root.ProjectPath("CMakeLists.txt"), cmake_after), "root CMakeLists readable after update");
        verdict.Add(cmake_after.find("# user tail outside fragments") != std::string::npos,
                    "outside bytes preserved after fragment refresh");
        verdict.Add(cmake_after.find("add_subdirectory(IDPlugin/Extra)") != std::string::npos
                        || cmake_after.find("Extra") != std::string::npos,
                    "plugin-targets fragment refreshed");

        // ② 未完成 attempt：`p6.after-manifest-rename=abort` 制造"manifest 已换、resolved 未换"窗口，
        //    随后 `recover --action restore` 只撤销受控片段范围，片段外内容保留
        verdict.Add(WritePluginsJson(root, "E7",
                                     "[ { \"name\": \"Sample\", \"linkage_type\": \"static\", \"options\": {} } ]"),
                    "selection changed back for the abort attempt");

        ID::Bench::IdProject::SpawnOptions options;
        options.inject     = "p6.after-manifest-rename=abort";
        options.timeout_ms = 60000;
        const ID::Bench::IdProject::SpawnResult aborted =
            ID::Bench::IdProject::SpawnProject(root, "e7-abort", { "project", "update", "E7" }, options);
        verdict.Add(aborted.is_spawned && !aborted.is_timed_out && aborted.exit_code == 124,
                    "abort injection exits 124");

        const CommandResult restore = RunProject(root, { "project", "recover", "E7", "--action", "restore" });
        AddExit(verdict, "recover --action restore exit0", restore, 0);

        std::string restored;
        verdict.Add(ReadText(root.ProjectPath("CMakeLists.txt"), restored)
                        && restored.find("# user tail outside fragments") != std::string::npos,
                    "outside bytes preserved after restore");
        verdict.Add(!IsRegularFile(ProjectRecordPath(root, "intent.json")), "intent removed after restore");

        FinishCase("id.project.e7", root, verdict);
    }
} // namespace

namespace ID::Bench
{
    void IdProjectCasesDE() noexcept
    {
        CaseD1();
        CaseD2();
        CaseD3();
        CaseD4();
        CaseD5();
        CaseD6();
        CaseD7();
        CaseE1();
        CaseE2();
        CaseE3();
        CaseE4();
        CaseE5();
        CaseE6();
        CaseE7();
    }
} // namespace ID::Bench
