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
 *  `id.project` 输入非法组（C1–C26）。
 *
 *  判定口径（`docs/id/11-verification-matrix.md` §11.4「输入非法」）：负例一律断言**具体类别名 + 退出码**，
 *  不接受"只看到失败"；`warn` 类别断言"无退出映射、不改变退出码"，不写成"码 0 就通过"。
 *  每个用例只出一条判定行，子条件（类别、码、零写）逐项记在 detail 里。
 */

namespace
{
    using ID::Bench::Check;
    using ID::Bench::IdProject::CaseRoot;
    using ID::Bench::IdProject::CaseVerdict;
    using ID::Bench::IdProject::CommandResult;
    using ID::Bench::IdProject::CountTreeEntries;
    using ID::Bench::IdProject::CreateDirectories;
    using ID::Bench::IdProject::CreateJunction;
    using ID::Bench::IdProject::HasCategory;
    using ID::Bench::IdProject::IsDirectory;
    using ID::Bench::IdProject::IsRegularFile;
    using ID::Bench::IdProject::JsonReader;
    using ID::Bench::IdProject::PluginSpec;
    using ID::Bench::IdProject::ProjectRecordPath;
    using ID::Bench::IdProject::ReadResolved;
    using ID::Bench::IdProject::ReadText;
    using ID::Bench::IdProject::RegistryPath;
    using ID::Bench::IdProject::RemoveTree;
    using ID::Bench::IdProject::RunProject;
    using ID::Bench::IdProject::SnapshotDiff;
    using ID::Bench::IdProject::TakeSnapshot;
    using ID::Bench::IdProject::WritePlugin;
    using ID::Bench::IdProject::WritePluginsJson;
    using ID::Bench::IdProject::WriteText;

    PluginSpec SimplePlugin(std::string name, std::string dependencies = "[]", std::string options = "[]",
                            std::string requirements = "[]")
    {
        PluginSpec spec;
        spec.name              = std::move(name);
        spec.dependencies_json = std::move(dependencies);
        spec.options_json      = std::move(options);
        spec.requirements_json = std::move(requirements);
        return spec;
    }

    /// @brief 清空项目根、登记与源插件库，让同一个用例内的多个样本互不影响
    void ResetFixture(const CaseRoot& root) noexcept
    {
        RemoveTree(root.project_root());
        RemoveTree(root.EnginePath("ID"));
        // 源插件库同样要清空：否则上一段样本写下的 `README.md` / `LICENSE` 会留在
        // `engine/IDPlugin/<name>/`，让"缺 README"这类样本被前一段夹具的资产满足
        RemoveTree(root.plugin_root());
        CreateDirectories(root.project_root());
        CreateDirectories(root.EnginePath("ID"));
        CreateDirectories(root.plugin_root());
    }

    void FinishCase(std::string_view name, const CaseRoot& root, const CaseVerdict& verdict) noexcept
    {
        Check(verdict.IsOk(), name, verdict.Detail());
        if (!verdict.IsOk())
        {
            root.ArchiveEvidence(std::string(name.substr(name.rfind('.') + 1)) + "-fail");
        }
    }

    /// @brief 登记文件的三种损坏形态
    std::string RegistryTruncated(const CaseRoot& root) noexcept
    {
        return "{ \"name\": \"C1\", \"path\": \"" + root.project_root() + "\", \"is_initialized\": ";
    }

    std::string RegistryMissingName(const CaseRoot& root) noexcept
    {
        return "[ { \"path\": \"" + root.project_root() + "\", \"is_initialized\": false } ]";
    }

    std::string RegistryDuplicateName(const CaseRoot& root) noexcept
    {
        return "[ { \"name\": \"C1\", \"path\": \"" + root.project_root() + "\", \"is_initialized\": false },\n"
               "  { \"name\": \"C1\", \"path\": \"" + root.project_root() + "-other\", \"is_initialized\": false } ]";
    }

    // ==================== 用例 ====================

    /// C1：`project.json` 截断 / 缺 name / 同名不同路径 → registry_invalid(1)，零写
    void CaseC1() noexcept
    {
        const CaseRoot root("c1");
        CaseVerdict    verdict;

        const std::string samples[3] = { RegistryTruncated(root), RegistryMissingName(root), RegistryDuplicateName(root) };
        const char* const labels[3]  = { "truncated", "missing-name", "duplicate-name" };

        for (std::size_t index = 0; index < 3; ++index)
        {
            ResetFixture(root);
            verdict.Add(WriteText(RegistryPath(root), samples[index]), std::string(labels[index]) + ".fixture");

            const ID::Bench::IdProject::Snapshot before = TakeSnapshot(root);
            const CommandResult result = RunProject(root, {"project", "new", "--name", "C1", "--path", root.project_root(), "--empty"});
            const ID::Bench::IdProject::Snapshot after = TakeSnapshot(root);

            verdict.Add(result.exit_code == 1, std::string(labels[index]) + ".exit1");
            verdict.Add(HasCategory(result, "registry_invalid"), std::string(labels[index]) + ".category");
            verdict.Add(SnapshotDiff(before, after).empty(), std::string(labels[index]) + ".zero-write");
        }

        FinishCase("id.project.c1", root, verdict);
    }

    /// C2：项目名 `1abc` / `a.b` / 含空格 → project_name_invalid(1)，不进入 P2
    void CaseC2() noexcept
    {
        const CaseRoot root("c2");
        CaseVerdict    verdict;

        const char* const names[3] = { "1abc", "a.b", "a b" };
        for (const char* const name : names)
        {
            ResetFixture(root);
            const CommandResult result =
                RunProject(root, {"project", "new", "--name", name, "--path", root.project_root(), "--empty"});
            ID::Bench::IdProject::AddExit(verdict, std::string(name) + ".exit1", result, 1);
            verdict.Add(HasCategory(result, "project_name_invalid"), std::string(name) + ".category");
            verdict.Add(!IsDirectory(root.ProjectPath("IDGenerated")), std::string(name) + ".no IDGenerated/");
            verdict.Add(!IsRegularFile(ProjectRecordPath(root, "intent.json")), std::string(name) + ".no intent");
        }

        FinishCase("id.project.c2", root, verdict);
    }

    /// C3：项目根等于假引擎根 / 位于其子目录 → project_path_overlap(1)
    void CaseC3() noexcept
    {
        const CaseRoot root("c3");
        CaseVerdict    verdict;

        const std::string engine_child = root.EnginePath("ID/sub");
        CreateDirectories(engine_child);

        const std::string samples[2] = { root.engine_root(), engine_child };
        for (const std::string& path : samples)
        {
            ResetFixture(root);
            const CommandResult result =
                RunProject(root, {"project", "new", "--name", "C3", "--path", path, "--empty"});
            verdict.Add(result.exit_code == 1, "path=" + path + ".exit1");
            verdict.Add(HasCategory(result, "project_path_overlap"), "path=" + path + ".category");
        }

        FinishCase("id.project.c3", root, verdict);
    }

    /// C4：`plugins.json` 五形态 → 全部 project_config_invalid(1)
    void CaseC4() noexcept
    {
        const CaseRoot root("c4");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");

        const std::string samples[5] = {
            "{ \"plugins\": [] }",
            "{ \"name\": 42, \"plugins\": [] }",
            "{ \"name\": \"C4\", \"plugins\": [ { \"name\": \"Sample\", \"linkage_type\": \"weird\", \"options\": {} } ] }",
            "{ \"name\": \"C4\", \"plugins\": [ { \"name\": \"Sample\", \"options\": {} } ] }",
            "{ \"name\": \"C4\", \"plugins\": [ { \"name\": \"Sample\", \"linkage_type\": \"static\" } ] }",
        };
        const char* const labels[5] = { "missing-name", "name-type", "linkage-value", "linkage-key", "options-key" };

        for (std::size_t index = 0; index < 5; ++index)
        {
            ResetFixture(root);
            verdict.Add(WritePlugin(root, SimplePlugin("Sample")), std::string(labels[index]) + ".plugin");
            ID::Bench::IdProject::AddExit(
                verdict, std::string(labels[index]) + ".register",
                RunProject(root, { "project", "new", "--name", "C4", "--path", root.project_root(), "--empty" }), 0);
            verdict.Add(WriteText(root.ProjectPath("plugins.json"), samples[index]), std::string(labels[index]) + ".fixture");
            const CommandResult result = RunProject(root, { "project", "update", "C4" });
            ID::Bench::IdProject::AddExit(verdict, std::string(labels[index]) + ".exit1", result, 1);
            ID::Bench::IdProject::CheckCategory(verdict, std::string(labels[index]) + ".category", result,
                                                "project_config_invalid");
        }

        FinishCase("id.project.c4", root, verdict);
    }

    /// C5：选项名未声明 / 取值 `on` → option_unknown(1) / option_value_invalid(1)
    void CaseC5() noexcept
    {
        const CaseRoot root("c5");
        CaseVerdict    verdict;

        PluginSpec spec = SimplePlugin(
            "Sample", "[]",
            "[ { \"name\": \"ENABLE_LOG\", \"type\": \"bool\", \"default\": \"OFF\", \"description\": \"fixture option\" } ]");
        verdict.Add(WritePlugin(root, spec), "fixture plugin Sample with option");

        ResetFixture(root);
        verdict.Add(WritePlugin(root, spec), "re-create plugin");
        ID::Bench::IdProject::AddExit(verdict, "register exit0",
                                      RunProject(root, { "project", "new", "--name", "C5", "--path", root.project_root(),
                                                         "--empty" }),
                                      0);
        verdict.Add(WritePluginsJson(root, "C5",
                                     "[ { \"name\": \"Sample\", \"linkage_type\": \"static\","
                                     " \"options\": { \"NOPE\": \"ON\" } } ]"),
                    "unknown option fixture");
        const CommandResult unknown = RunProject(root, {"project", "update", "C5"});
        ID::Bench::IdProject::AddExit(verdict, "unknown option exit1", unknown, 1);
        ID::Bench::IdProject::CheckCategory(verdict, "unknown option -> option_unknown", unknown, "option_unknown");

        verdict.Add(WritePluginsJson(root, "C5",
                                     "[ { \"name\": \"Sample\", \"linkage_type\": \"static\","
                                     " \"options\": { \"ENABLE_LOG\": \"on\" } } ]"),
                    "lower-case value fixture");
        const CommandResult invalid = RunProject(root, {"project", "update", "C5"});
        ID::Bench::IdProject::AddExit(verdict, "value on exit1", invalid, 1);
        ID::Bench::IdProject::CheckCategory(verdict, "value on -> option_value_invalid", invalid, "option_value_invalid");
        verdict.Add(HasCategory(invalid, "ON") && HasCategory(invalid, "OFF"), "diagnostic lists legal value set");

        FinishCase("id.project.c5", root, verdict);
    }

    /// C6：`requirements` 声明 SHARED_ONLY 而选择 static → requirement_conflict(1)
    void CaseC6() noexcept
    {
        const CaseRoot root("c6");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Shared", "[]", "[]", "[ \"SHARED_ONLY\" ]")),
                    "fixture plugin with SHARED_ONLY");
        ID::Bench::IdProject::AddExit(verdict, "register exit0",
                                      RunProject(root, { "project", "new", "--name", "C6", "--path", root.project_root(),
                                                         "--empty" }),
                                      0);
        // 用户显式选择 static 与清单的 SHARED_ONLY 冲突（CLI 的 --plugin 会按 requirements 展开缺省形态）
        verdict.Add(WritePluginsJson(root, "C6",
                                     "[ { \"name\": \"Shared\", \"linkage_type\": \"static\", \"options\": {} } ]"),
                    "explicit static selection");

        const CommandResult result = RunProject(root, { "project", "update", "C6" });
        ID::Bench::IdProject::AddExit(verdict, "exit1", result, 1);
        ID::Bench::IdProject::CheckCategory(verdict, "category requirement_conflict", result, "requirement_conflict");
        verdict.Add(HasCategory(result, "SHARED_ONLY"), "diagnostic shows expected requirement");

        FinishCase("id.project.c6", root, verdict);
    }

    /// C7：两份清单互相依赖成环 → dependency_cycle(1)，actual 含环路序列
    void CaseC7() noexcept
    {
        const CaseRoot root("c7");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("A", "[ \"B\" ]")), "fixture A -> B");
        verdict.Add(WritePlugin(root, SimplePlugin("B", "[ \"A\" ]")), "fixture B -> A");

        const CommandResult result =
            RunProject(root, {"project", "new", "--name", "C7", "--path", root.project_root(), "--plugin", "A"});
        ID::Bench::IdProject::AddExit(verdict, "exit1", result, 1);
        ID::Bench::IdProject::CheckCategory(verdict, "category dependency_cycle", result, "dependency_cycle");
        const bool has_sequence = HasCategory(result, "A → B") || HasCategory(result, "B → A")
                               || HasCategory(result, "A -> B") || HasCategory(result, "B -> A");
        verdict.Add(has_sequence, "actual contains cycle sequence");
        if (!has_sequence)
        {
            verdict.Note("actual", result.err_text.substr(0, 200));
        }

        FinishCase("id.project.c7", root, verdict);
    }

    /// C8：选择未知插件 → plugin_unknown(1)，actual 含发现期原始原因
    void CaseC8() noexcept
    {
        const CaseRoot root("c8");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");

        const CommandResult result =
            RunProject(root, {"project", "new", "--name", "C8", "--path", root.project_root(), "--plugin", "ID.Missing"});
        verdict.Add(result.exit_code == 1, "exit1");
        verdict.Add(HasCategory(result, "plugin_unknown"), "category plugin_unknown");
        verdict.Add(HasCategory(result, "ID.Missing"), "diagnostic names the plugin");

        FinishCase("id.project.c8", root, verdict);
    }

    /// C9：选中插件的清单格式非法 → plugin_config_invalid(warn) + plugin_unknown(1)
    void CaseC9() noexcept
    {
        const CaseRoot root("c9");
        CaseVerdict    verdict;

        PluginSpec broken;
        broken.name              = "Broken";
        broken.manifest_override = "{ \"name\": \"Broken\", \"dependencies\": [ }";
        verdict.Add(WritePlugin(root, broken), "fixture plugin with illegal manifest");

        const CommandResult result =
            RunProject(root, {"project", "new", "--name", "C9", "--path", root.project_root(), "--plugin", "Broken"});
        verdict.Add(result.exit_code == 1, "exit1");
        verdict.Add(HasCategory(result, "plugin_config_invalid"), "warn plugin_config_invalid present");
        verdict.Add(HasCategory(result, "plugin_unknown"), "plugin_unknown(1) for the reference");
        verdict.Add(HasCategory(result, "Broken"), "diagnostic names the ignored plugin");

        FinishCase("id.project.c9", root, verdict);
    }

    /// C10：未选中插件清单非法 → 仅 warn，退出码 0，且不在 resolved.selection
    void CaseC10() noexcept
    {
        const CaseRoot root("c10");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        const CommandResult create =
            RunProject(root, {"project", "new", "--name", "C10", "--path", root.project_root(), "--plugin", "Sample"});
        verdict.Add(create.exit_code == 0, "initial new exit0");

        PluginSpec broken;
        broken.name              = "Broken";
        broken.manifest_override = "{ \"name\": \"Broken\", \"dependencies\": [ }";
        verdict.Add(WritePlugin(root, broken), "add illegal unselected plugin");

        const CommandResult update = RunProject(root, {"project", "update", "C10"});
        verdict.Add(update.exit_code == 0, "update exit0 (warn only)");
        verdict.Add(HasCategory(update, "plugin_config_invalid"), "warn plugin_config_invalid");

        JsonReader resolved;
        verdict.Add(ReadResolved(root, resolved), "resolved readable");
        const std::size_t selection_count = resolved.Count("selection");
        bool              is_listed       = false;
        for (std::size_t index = 0; index < selection_count; ++index)
        {
            is_listed = is_listed || resolved.String("selection." + std::to_string(index) + ".plugin") == "Broken";
        }
        verdict.Add(!is_listed, "Broken not in resolved.selection");

        FinishCase("id.project.c10", root, verdict);
    }

    /// C11：声明条目不存在 → asset_missing(1)；缺 README.md → plugin_asset_incomplete(1)
    void CaseC11() noexcept
    {
        const CaseRoot root("c11");
        CaseVerdict    verdict;

        PluginSpec missing_file = SimplePlugin("Sample");
        missing_file.files_json = "[ \"LICENSE\", \"NOT-THERE.txt\" ]";
        verdict.Add(WritePlugin(root, missing_file), "fixture plugin declaring a missing file");
        const CommandResult first =
            RunProject(root, {"project", "new", "--name", "C11", "--path", root.project_root(), "--plugin", "Sample"});
        ID::Bench::IdProject::AddExit(verdict, "missing declared file: exit1", first, 1);
        ID::Bench::IdProject::CheckCategory(verdict, "missing declared file: asset_missing", first, "asset_missing");
        verdict.Add(HasCategory(first, "NOT-THERE.txt"), "diagnostic lists the missing entry");

        ResetFixture(root);
        PluginSpec no_readme = SimplePlugin("Sample");
        no_readme.has_readme = false;
        verdict.Add(WritePlugin(root, no_readme), "fixture plugin without README.md");
        verdict.Add(!IsRegularFile(root.plugin_root() + "/Sample/README.md"),
                    "fixture really has no README.md before new");
        const CommandResult second =
            RunProject(root, {"project", "new", "--name", "C11", "--path", root.project_root(), "--plugin", "Sample"});
        ID::Bench::IdProject::AddExit(verdict, "missing README: exit1", second, 1);
        ID::Bench::IdProject::CheckCategory(verdict, "missing README: plugin_asset_incomplete", second,
                                            "plugin_asset_incomplete");

        FinishCase("id.project.c11", root, verdict);
    }

    /// C12：`header_dirs` 含 `..` / 绝对路径 / 指向 junction → path_outside_project(1)
    void CaseC12() noexcept
    {
        const CaseRoot root("c12");
        CaseVerdict    verdict;

        // 样本一：含 ..
        ResetFixture(root);
        PluginSpec dotdot = SimplePlugin("Sample");
        dotdot.header_dirs_json = "[ \"../outside\" ]";
        verdict.Add(WritePlugin(root, dotdot), "fixture header_dirs with ..");
        const CommandResult first =
            RunProject(root, {"project", "new", "--name", "C12", "--path", root.project_root(), "--plugin", "Sample"});
        ID::Bench::IdProject::AddExit(verdict, "`..` exit1", first, 1);
        ID::Bench::IdProject::CheckCategory(verdict, "`..` -> path_outside_project", first, "path_outside_project");

        // 样本二：绝对路径
        ResetFixture(root);
        PluginSpec absolute = SimplePlugin("Sample");
        absolute.header_dirs_json = "[ \"" + root.CasePath("outside-include") + "\" ]";
        CreateDirectories(root.CasePath("outside-include"));
        verdict.Add(WritePlugin(root, absolute), "fixture header_dirs with absolute path");
        const CommandResult second =
            RunProject(root, {"project", "new", "--name", "C12", "--path", root.project_root(), "--plugin", "Sample"});
        ID::Bench::IdProject::AddExit(verdict, "absolute path exit1", second, 1);
        ID::Bench::IdProject::CheckCategory(verdict, "absolute path -> path_outside_project", second,
                                            "path_outside_project");

        // 样本三：指向 junction
        ResetFixture(root);
        PluginSpec junction = SimplePlugin("Sample");
        junction.header_dirs_json = "[ \"linked\" ]";
        verdict.Add(WritePlugin(root, junction), "fixture header_dirs entry to be turned into a junction");
        const bool is_junction =
            CreateJunction(root.plugin_root() + "/Sample/linked", root.CasePath("outside-linked"));
        if (!is_junction)
        {
            ID::Bench::Uncovered("id.project.c12.junction",
                                 "环境不可构造：`cmd /c mklink /J` 未能创建目录 junction（§11.6 U-5 同类），"
                                 "该子项不参与判定");
            verdict.Add(true, "junction sub-sample uncovered");
        }
        else
        {
            const CommandResult third =
                RunProject(root, {"project", "new", "--name", "C12", "--path", root.project_root(), "--plugin", "Sample"});
            ID::Bench::IdProject::AddExit(verdict, "junction exit1", third, 1);
            ID::Bench::IdProject::CheckCategory(verdict, "junction -> path_outside_project", third,
                                                "path_outside_project");
        }

        FinishCase("id.project.c12", root, verdict);
    }

    /// C13：项目名与插件目标同名 / 两个插件折叠出同一 TAG → cmake_name_conflict(1)
    void CaseC13() noexcept
    {
        const CaseRoot root("c13");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Same")), "fixture plugin Same");
        const CommandResult first =
            RunProject(root, {"project", "new", "--name", "Same", "--path", root.project_root(), "--plugin", "Same"});
        verdict.Add(first.exit_code == 1 && HasCategory(first, "cmake_name_conflict"),
                    "project name == plugin target -> cmake_name_conflict(1)");

        // 第二个样本：IDFoo 与 Foo 折叠出同一 TAG，且两者都声明同名选项
        ResetFixture(root);
        const std::string option =
            "[ { \"name\": \"ENABLE_LOG\", \"type\": \"bool\", \"default\": \"OFF\", \"description\": \"fixture\" } ]";
        verdict.Add(WritePlugin(root, SimplePlugin("IDFoo", "[]", option)), "fixture IDFoo with option");
        verdict.Add(WritePlugin(root, SimplePlugin("Foo", "[]", option)), "fixture Foo with option");
        const CommandResult second =
            RunProject(root, {"project", "new", "--name", "C13", "--path", root.project_root(), "--plugin", "IDFoo",
                              "--plugin", "Foo"});
        ID::Bench::IdProject::AddExit(verdict, "same TAG exit1", second, 1);
        ID::Bench::IdProject::CheckCategory(verdict, "same TAG -> cmake_name_conflict", second, "cmake_name_conflict");
        verdict.Add(HasCategory(second, "IDFOO_ENABLE_LOG") || HasCategory(second, "ENABLE_LOG"),
                    "diagnostic mentions option variable");
        // 形态宏按 spec §2.4 是 `ID_<TAG>_STATIC` / `ID_<TAG>_EXPORTS`：`IDFoo`/`Foo` 折叠出的 `TAG` 是 `FOO`，
        // 因此这两个宏分别为 `ID_FOO_STATIC` / `ID_FOO_EXPORTS`（与选项变量 `IDFOO_ENABLE_LOG` 同一 `TAG` 推导）
        const bool has_shape_macro = HasCategory(second, "ID_FOO_STATIC") || HasCategory(second, "ID_FOO_EXPORTS");
        verdict.Add(has_shape_macro, "diagnostic mentions shape macros");
        if (!has_shape_macro)
        {
            verdict.Note("actual", second.err_text.substr(0, 200));
        }

        FinishCase("id.project.c13", root, verdict);
    }

    /// C14：已有 `plugins.json` 时给 `--plugin` 或 `--empty` → selection_source_conflict(3)
    void CaseC14() noexcept
    {
        const CaseRoot root("c14");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        verdict.Add(WritePluginsJson(root, "C14",
                                     "[ { \"name\": \"Sample\", \"linkage_type\": \"static\", \"options\": {} } ]"),
                    "existing plugins.json");

        const ID::Bench::IdProject::Snapshot before = TakeSnapshot(root);
        const CommandResult with_plugin =
            RunProject(root, {"project", "new", "--name", "C14", "--path", root.project_root(), "--plugin", "Sample"});
        const CommandResult with_empty =
            RunProject(root, {"project", "new", "--name", "C14", "--path", root.project_root(), "--empty"});
        const ID::Bench::IdProject::Snapshot after = TakeSnapshot(root);

        verdict.Add(with_plugin.exit_code == 3 && HasCategory(with_plugin, "selection_source_conflict"),
                    "--plugin -> selection_source_conflict(3)");
        verdict.Add(with_empty.exit_code == 3 && HasCategory(with_empty, "selection_source_conflict"),
                    "--empty -> selection_source_conflict(3)");
        verdict.Add(SnapshotDiff(before, after).empty(), "zero write");

        FinishCase("id.project.c14", root, verdict);
    }

    /// C15：两个选项的 links_when_on 指向同一上游 → 单一链接、不按声明顺序择一
    void CaseC15() noexcept
    {
        const CaseRoot root("c15");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("U")), "fixture upstream U");
        const std::string options =
            "[ { \"name\": \"OP1\", \"type\": \"bool\", \"default\": \"OFF\", \"description\": \"fixture\","
            " \"links_when_on\": [ \"U\" ] },"
            " { \"name\": \"OP2\", \"type\": \"bool\", \"default\": \"OFF\", \"description\": \"fixture\","
            " \"links_when_on\": [ \"U\" ] } ]";
        verdict.Add(WritePlugin(root, SimplePlugin("P", "[ \"U\" ]", options)), "fixture plugin P with two options");

        const auto generated_text = [&root]() -> std::string
        {
            std::string text;
            ReadText(root.ProjectPath("IDPlugin/P/CMakeLists.txt"), text);
            return text;
        };

        const auto count_of = [](const std::string& text, std::string_view needle) -> std::size_t
        {
            std::size_t count = 0;
            std::size_t begin = 0;
            while ((begin = text.find(needle, begin)) != std::string::npos)
            {
                ++count;
                begin += needle.size();
            }
            return count;
        };

        // 样本一：只开 OP1
        ID::Bench::IdProject::AddExit(verdict, "sample1 register exit0",
                                      RunProject(root, { "project", "new", "--name", "C15", "--path", root.project_root(),
                                                         "--empty" }),
                                      0);
        verdict.Add(WritePluginsJson(root, "C15",
                                     "[ { \"name\": \"P\", \"linkage_type\": \"static\","
                                     " \"options\": { \"OP1\": \"ON\", \"OP2\": \"OFF\" } } ]"),
                    "sample1: OP1 only");
        const CommandResult sample1 = RunProject(root, {"project", "update", "C15"});
        const std::string text1     = generated_text();
        ID::Bench::IdProject::AddExit(verdict, "sample1 exit0", sample1, 0);
        verdict.Add(text1.find("U") != std::string::npos, "sample1 links upstream U");
        const std::size_t link_count1 = count_of(text1, " U");

        // 样本二：全关
        ResetFixture(root);
        verdict.Add(WritePlugin(root, SimplePlugin("U")), "re-create upstream U");
        verdict.Add(WritePlugin(root, SimplePlugin("P", "[ \"U\" ]", options)), "re-create plugin P");
        ID::Bench::IdProject::AddExit(verdict, "sample2 register exit0",
                                      RunProject(root, { "project", "new", "--name", "C15", "--path", root.project_root(),
                                                         "--empty" }),
                                      0);
        verdict.Add(WritePluginsJson(root, "C15",
                                     "[ { \"name\": \"P\", \"linkage_type\": \"static\","
                                     " \"options\": { \"OP1\": \"OFF\", \"OP2\": \"OFF\" } } ]"),
                    "sample2: both off");
        const CommandResult sample2 = RunProject(root, {"project", "update", "C15"});
        const std::string text2     = generated_text();
        ID::Bench::IdProject::AddExit(verdict, "sample2 exit0", sample2, 0);
        // 契约是"两个 `links_when_on` 指向同一上游时只出现一个链接项、不按声明顺序择一、也不重复"
        // （矩阵 C15）。P 同时把 U 声明为 `dependencies`，因此全关样本里那条链接来自依赖本身，
        // 条件是 **U 只被链接一次**；不是"文本里不再出现 U"（那是把依赖也当成漏写），
        // 也不要求此时出现 OR 条件块（条件形态只在选项打开时出现）
        const std::size_t link_items2 = count_of(text2, " U");
        verdict.Add(link_items2 == 1, "sample2 links U exactly once (no per-option duplicate)");
        if (link_items2 != 1)
        {
            verdict.Note("sample2-link-occurrences", std::to_string(link_items2));
            verdict.Note("sample2-tail", text2.size() > 400 ? text2.substr(text2.size() - 400) : text2);
        }

        // 样本三：全开
        ResetFixture(root);
        verdict.Add(WritePlugin(root, SimplePlugin("U")), "re-create upstream U (sample3)");
        verdict.Add(WritePlugin(root, SimplePlugin("P", "[ \"U\" ]", options)), "re-create plugin P (sample3)");
        ID::Bench::IdProject::AddExit(verdict, "sample3 register exit0",
                                      RunProject(root, { "project", "new", "--name", "C15", "--path", root.project_root(),
                                                         "--empty" }),
                                      0);
        verdict.Add(WritePluginsJson(root, "C15",
                                     "[ { \"name\": \"P\", \"linkage_type\": \"static\","
                                     " \"options\": { \"OP1\": \"ON\", \"OP2\": \"ON\" } } ]"),
                    "sample3: both on");
        const CommandResult sample3 = RunProject(root, {"project", "update", "C15"});
        const std::string text3     = generated_text();
        ID::Bench::IdProject::AddExit(verdict, "sample3 exit0", sample3, 0);
        verdict.Add(count_of(text3, " U") == 1, "sample3 links upstream exactly once");

        JsonReader resolved;
        verdict.Add(ReadResolved(root, resolved), "resolved readable");
        bool is_edge_annotated = false;
        const std::size_t edge_count = resolved.Count("edges");
        for (std::size_t index = 0; index < edge_count; ++index)
        {
            const std::string prefix = "edges." + std::to_string(index) + ".";
            if (resolved.String(prefix + "from") != "P" || resolved.String(prefix + "to") != "U")
            {
                continue;
            }
            const std::size_t option_count = resolved.Count(prefix + "controlling_options");
            is_edge_annotated              = option_count == 2;
        }
        verdict.Add(is_edge_annotated, "edge P -> U annotated with both controlling options");
        verdict.Note("sample1-link-occurrences", std::to_string(link_count1));

        FinishCase("id.project.c15", root, verdict);
    }

    /// C16：同一 `--plugin` 在一次 `new` 里给两次 → cli_usage(3)，不合并不去重
    void CaseC16() noexcept
    {
        const CaseRoot root("c16");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        const CommandResult result =
            RunProject(root, {"project", "new", "--name", "C16", "--path", root.project_root(), "--plugin", "Sample",
                              "--plugin", "Sample"});
        verdict.Add(result.exit_code == 3, "exit3");
        verdict.Add(HasCategory(result, "cli_usage"), "category cli_usage");
        verdict.Add(!IsDirectory(root.ProjectPath("IDGenerated")), "no IDGenerated/");

        FinishCase("id.project.c16", root, verdict);
    }

    /// C17：`--path` 给相对路径 → project_path_invalid(1)，原值逐字出现在诊断里
    void CaseC17() noexcept
    {
        const CaseRoot root("c17");
        CaseVerdict    verdict;

        const std::string relative = "relative-project";
        const CommandResult result =
            RunProject(root, {"project", "new", "--name", "C17", "--path", relative, "--empty"});
        verdict.Add(result.exit_code == 1, "exit1");
        verdict.Add(HasCategory(result, "project_path_invalid"), "category project_path_invalid");
        verdict.Add(HasCategory(result, relative), "diagnostic prints the received value verbatim");
        verdict.Add(!IsDirectory(root.CasePath(relative)), "does not resolve against working directory");

        FinishCase("id.project.c17", root, verdict);
    }

    /// C18：插件名折叠出非法 CMake 标识符（`ID-Example` → `-EXAMPLE`）→ cmake_name_conflict(1)
    void CaseC18() noexcept
    {
        const CaseRoot root("c18");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("ID-Example")), "fixture plugin ID-Example");
        const CommandResult result =
            RunProject(root, {"project", "new", "--name", "C18", "--path", root.project_root(), "--plugin", "ID-Example"});
        verdict.Add(result.exit_code == 1, "exit1");
        verdict.Add(HasCategory(result, "cmake_name_conflict"), "category cmake_name_conflict");
        verdict.Add(HasCategory(result, "ID-Example"), "diagnostic names the plugin");

        std::string generated;
        ReadText(root.ProjectPath("IDPlugin/ID-Example/CMakeLists.txt"), generated);
        verdict.Add(generated.empty() || generated.find("EXAMPLE") == std::string::npos,
                    "generated text does not contain a sanitized name");

        FinishCase("id.project.c18", root, verdict);
    }

    /// C19：`--plugin` 与 `--empty` 同时给 → cli_usage(3)，不进组装、零写
    void CaseC19() noexcept
    {
        const CaseRoot root("c19");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        const ID::Bench::IdProject::Snapshot before = TakeSnapshot(root);
        const CommandResult result =
            RunProject(root, {"project", "new", "--name", "C19", "--path", root.project_root(), "--plugin", "Sample",
                              "--empty"});
        const ID::Bench::IdProject::Snapshot after = TakeSnapshot(root);

        verdict.Add(result.exit_code == 3 && HasCategory(result, "cli_usage"), "cli_usage(3)");
        verdict.Add(SnapshotDiff(before, after).empty(), "zero write");

        FinishCase("id.project.c19", root, verdict);
    }

    /// C20：同一参数重复且取值不同 → cli_usage(3)；取值相同视为同一请求
    void CaseC20() noexcept
    {
        const CaseRoot root("c20");
        CaseVerdict    verdict;

        const CommandResult different =
            RunProject(root, {"project", "new", "--name", "C20", "--path", root.project_root(), "--empty",
                              "--generator", "MinGW Makefiles", "--generator", "Ninja"});
        verdict.Add(different.exit_code == 3 && HasCategory(different, "cli_usage"), "different values -> cli_usage(3)");

        ResetFixture(root);
        const CommandResult same =
            RunProject(root, {"project", "new", "--name", "C20", "--path", root.project_root(), "--empty",
                              "--generator", "MinGW Makefiles", "--generator", "MinGW Makefiles"});
        verdict.Add(same.exit_code == 0, "same values -> same request, exit0");

        FinishCase("id.project.c20", root, verdict);
    }

    /// C21：`update`/`build`/`clean`/`recover` 指定未登记项目 → project_not_found(1)
    void CaseC21() noexcept
    {
        const CaseRoot root("c21");
        CaseVerdict    verdict;

        const CommandResult update = RunProject(root, {"project", "update", "Ghost"});
        const CommandResult build =
            RunProject(root, {"project", "build", "Ghost", "--config", "debug"});
        const CommandResult clean  = RunProject(root, {"project", "clean", "Ghost"});
        const CommandResult recover = RunProject(root, {"project", "recover", "Ghost", "--action", "continue"});

        verdict.Add(update.exit_code == 1 && HasCategory(update, "project_not_found"), "update -> project_not_found(1)");
        verdict.Add(build.exit_code == 1 && HasCategory(build, "project_not_found"), "build -> project_not_found(1)");
        verdict.Add(clean.exit_code == 1 && HasCategory(clean, "project_not_found"), "clean -> project_not_found(1)");
        verdict.Add(recover.exit_code == 1 && HasCategory(recover, "project_not_found"),
                    "recover -> project_not_found(1)");

        FinishCase("id.project.c21", root, verdict);
    }

    /// C22：登记 false、无记录、无 intent 的项目直接 `build` → project_not_initialized(1)
    void CaseC22() noexcept
    {
        const CaseRoot root("c22");
        CaseVerdict    verdict;

        const std::string registry =
            "[ { \"name\": \"C22\", \"path\": \"" + root.project_root() + "\", \"is_initialized\": false } ]";
        verdict.Add(WriteText(RegistryPath(root), registry), "fixture registry with is_initialized=false");

        const CommandResult result = RunProject(root, {"project", "build", "C22", "--config", "debug"});
        verdict.Add(result.exit_code == 1, "exit1");
        verdict.Add(HasCategory(result, "project_not_initialized"), "category project_not_initialized");
        verdict.Add(!IsDirectory(root.ProjectPath("build")), "no build/");

        FinishCase("id.project.c22", root, verdict);
    }

    /// C23：删掉 `plugins.json` 后 `update`/`build` 报 project_config_missing(1)，`clean` 不受影响
    void CaseC23() noexcept
    {
        const CaseRoot root("c23");
        CaseVerdict    verdict;

        const CommandResult create =
            RunProject(root, {"project", "new", "--name", "C23", "--path", root.project_root(), "--empty"});
        verdict.Add(create.exit_code == 0, "initial new exit0");

        verdict.Add(RemoveTree(root.ProjectPath("plugins.json")), "delete plugins.json");

        const CommandResult update = RunProject(root, {"project", "update", "C23"});
        const CommandResult build  = RunProject(root, {"project", "build", "C23", "--config", "debug"});
        const CommandResult clean  = RunProject(root, {"project", "clean", "C23"});

        verdict.Add(update.exit_code == 1 && HasCategory(update, "project_config_missing"),
                    "update -> project_config_missing(1)");
        verdict.Add(build.exit_code == 1 && HasCategory(build, "project_config_missing"),
                    "build -> project_config_missing(1)");
        verdict.Add(clean.exit_code == 0 || clean.exit_code == 6, "clean exit 0 or 6");
        verdict.Add(!HasCategory(clean, "project_config_missing"), "clean does not report project_config_missing");

        FinishCase("id.project.c23", root, verdict);
    }

    /// C24：`plugins.json` 的 name 与登记名不一致 → project_name_mismatch(1)
    void CaseC24() noexcept
    {
        const CaseRoot root("c24");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        ID::Bench::IdProject::AddExit(
            verdict, "register exit0",
            RunProject(root, { "project", "new", "--name", "C24", "--path", root.project_root(), "--empty" }), 0);
        verdict.Add(WritePluginsJson(root, "OtherName",
                                     "[ { \"name\": \"Sample\", \"linkage_type\": \"static\", \"options\": {} } ]"),
                    "plugins.json with a different name");

        const CommandResult result = RunProject(root, { "project", "update", "C24" });
        ID::Bench::IdProject::AddExit(verdict, "exit1", result, 1);
        ID::Bench::IdProject::CheckCategory(verdict, "category project_name_mismatch", result, "project_name_mismatch");
        verdict.Add(HasCategory(result, "OtherName") && HasCategory(result, "C24"), "diagnostic lists both names");

        FinishCase("id.project.c24", root, verdict);
    }

    /// C25：同一插件在 `plugins.json` 里写两次 → plugin_duplicate(1)
    void CaseC25() noexcept
    {
        const CaseRoot root("c25");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        ID::Bench::IdProject::AddExit(
            verdict, "register exit0",
            RunProject(root, { "project", "new", "--name", "C25", "--path", root.project_root(), "--empty" }), 0);
        verdict.Add(WritePluginsJson(root, "C25",
                                     "[ { \"name\": \"Sample\", \"linkage_type\": \"static\", \"options\": {} },\n"
                                     "  { \"name\": \"Sample\", \"linkage_type\": \"static\", \"options\": {} } ]"),
                    "duplicate plugin entries");

        const CommandResult result = RunProject(root, { "project", "update", "C25" });
        ID::Bench::IdProject::AddExit(verdict, "exit1", result, 1);
        ID::Bench::IdProject::CheckCategory(verdict, "category plugin_duplicate", result, "plugin_duplicate");

        FinishCase("id.project.c25", root, verdict);
    }

    /// C26：清单 `dependencies` 引用不存在的插件；未引用仅 warn，被引用另加 plugin_unknown(1)
    void CaseC26() noexcept
    {
        const CaseRoot root("c26");
        CaseVerdict    verdict;

        verdict.Add(WritePlugin(root, SimplePlugin("Sample")), "fixture plugin Sample");
        verdict.Add(WritePlugin(root, SimplePlugin("Broken", "[ \"Ghost\" ]")), "fixture plugin referencing Ghost");

        const CommandResult unreferenced =
            RunProject(root, {"project", "new", "--name", "C26", "--path", root.project_root(), "--plugin", "Sample"});
        verdict.Add(unreferenced.exit_code == 0, "unreferenced broken plugin: exit0");
        verdict.Add(HasCategory(unreferenced, "dependency_unknown"), "unreferenced: dependency_unknown warn");

        ResetFixture(root);
        verdict.Add(WritePlugin(root, SimplePlugin("Broken", "[ \"Ghost\" ]")), "re-create broken plugin");
        const CommandResult referenced =
            RunProject(root, {"project", "new", "--name", "C26", "--path", root.project_root(), "--plugin", "Broken"});
        verdict.Add(referenced.exit_code == 1, "referenced: exit1");
        verdict.Add(HasCategory(referenced, "plugin_unknown"), "referenced: plugin_unknown(1)");
        verdict.Add(HasCategory(referenced, "Ghost"), "referenced: diagnostic keeps the original reason");

        FinishCase("id.project.c26", root, verdict);
    }
} // namespace

namespace ID::Bench
{
    void IdProjectCasesC() noexcept
    {
        CaseC1();
        CaseC2();
        CaseC3();
        CaseC4();
        CaseC5();
        CaseC6();
        CaseC7();
        CaseC8();
        CaseC9();
        CaseC10();
        CaseC11();
        CaseC12();
        CaseC13();
        CaseC14();
        CaseC15();
        CaseC16();
        CaseC17();
        CaseC18();
        CaseC19();
        CaseC20();
        CaseC21();
        CaseC22();
        CaseC23();
        CaseC24();
        CaseC25();
        CaseC26();
    }
} // namespace ID::Bench
