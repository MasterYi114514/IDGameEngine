#include "project/Input/Selection.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "project/Base/FileIo.hpp"
#include "project/Base/PathText.hpp"
#include "project/Base/Text.hpp"
#include "project/Record/RecordJson.hpp"

/**
 *  `plugins.json` 的严格结构校验与序列化。
 *
 *  未知字段一律拒绝（`skills/id-kernel/SKILL.md` 第 6 节第 2 步）：允许键集合写在本文件，新增字段
 *  必须先改契约源。选项名是否被清单声明不在此判定（那属于解析阶段）。
 */

namespace ID::Project
{
    namespace
    {
        constexpr std::string_view kSelectionSubject = "plugins.json";

        // 顶层允许键与插件项允许键：其余一律按未知字段拒绝
        constexpr std::string_view kTopLevelKeys[]  = {"name", "plugins"};
        constexpr std::string_view kPluginKeys[]    = {"name", "linkage_type", "options"};

        bool is_known_key(std::string_view key, std::span<const std::string_view> allowed) noexcept
        {
            for (const std::string_view candidate : allowed)
            {
                if (candidate == key)
                {
                    return true;
                }
            }
            return false;
        }

        // 未知字段：逐个列出键名，不做"忽略未知键"的宽容分支
        void append_unknown_keys(const ID::Json::JsonValue& object, std::span<const std::string_view> allowed,
                                 std::string_view path, std::vector<FieldIssue>& issues)
        {
            if (!object.is_object())
            {
                return;
            }
            for (const ID::Json::Detail::ObjectEntry* entry = object.members_begin();
                 entry != object.members_end(); ++entry)
            {
                const std::string_view key(entry->key, entry->key_length);
                if (is_known_key(key, allowed))
                {
                    continue;
                }
                issues.push_back(FieldIssue{std::string(path) + "." + std::string(key), "已知字段",
                                            std::string("未知字段")});
            }
        }

        // 选项对象：键为选项名、值为字符串；保留文件顺序
        bool read_options(const ID::Json::JsonValue& options, std::string_view path,
                          std::vector<FieldIssue>& issues, std::vector<OptionAssignment>& out)
        {
            if (!options.is_object())
            {
                issues.push_back(FieldIssue{std::string(path), "object", std::string(json_type_name(options.type()))});
                return false;
            }

            std::vector<OptionAssignment> result;
            result.reserve(options.size());
            for (const ID::Json::Detail::ObjectEntry* entry = options.members_begin();
                 entry != options.members_end(); ++entry)
            {
                const std::string_view key(entry->key, entry->key_length);
                const std::string item_path = std::string(path) + "." + std::string(key);
                if (key.empty())
                {
                    issues.push_back(FieldIssue{item_path, "非空选项名", std::string("空键")});
                    return false;
                }
                if (!entry->value.is_string())
                {
                    issues.push_back(FieldIssue{item_path, "string",
                                                std::string(json_type_name(entry->value.type()))});
                    return false;
                }
                OptionAssignment assignment;
                assignment.name  = std::string(key);
                assignment.value = std::string(entry->value.as_string());
                result.push_back(std::move(assignment));
            }
            out = std::move(result);
            return true;
        }

    } // namespace

    SelectionFile load_selection(const std::filesystem::path& path)
    {
        SelectionFile selection;
        const FileRead read = read_file(path);
        if (read.status == ReadStatus::NotFound)
        {
            selection.is_present = false;
            selection.is_valid   = false;
            return selection;
        }

        selection.is_present = true;
        if (read.status == ReadStatus::Failed)
        {
            selection.diagnostics.add(ProjectDiagnostic{ProjectCategory::ProjectConfigInvalid,
                                                        std::string(kSelectionSubject),
                                                        std::string("可读的选择文件"),
                                                        std::string("读取失败"),
                                                        to_utf8(path),
                                                        std::string("检查文件权限后重跑")});
            return selection;
        }

        std::vector<FieldIssue> issues;
        ID::Json::JsonDocument  document;
        const ID::Json::JsonValue* root = parse_json_root(document, read.bytes, kSelectionSubject, issues);
        if (root != nullptr)
        {
            append_unknown_keys(*root, kTopLevelKeys, kSelectionSubject, issues);

            JsonObjectReader reader(*root, kSelectionSubject, issues);
            reader.read_string("name", selection.name);

            const ID::Json::JsonValue* plugins = reader.read_array("plugins");
            if (plugins != nullptr)
            {
                selection.plugins.reserve(plugins->size());
                for (std::size_t index = 0; index < plugins->size(); ++index)
                {
                    const ID::Json::JsonValue& element = (*plugins)[index];
                    const std::string item_path = reader.index(index);

                    append_unknown_keys(element, kPluginKeys, item_path, issues);
                    JsonObjectReader item_reader(element, item_path, issues);

                    SelectionPlugin plugin;
                    item_reader.read_string("name", plugin.name);

                    std::string linkage_text;
                    if (item_reader.read_string("linkage_type", linkage_text)
                        && !parse_linkage(linkage_text, plugin.linkage))
                    {
                        issues.push_back(FieldIssue{item_reader.child("linkage_type"), "\"static\" 或 \"dynamic\"",
                                                    linkage_text});
                    }

                    const ID::Json::JsonValue* options = item_reader.read_object("options");
                    if (options != nullptr)
                    {
                        read_options(*options, item_reader.child("options"), issues, plugin.options);
                    }
                    selection.plugins.push_back(std::move(plugin));
                }
            }
        }

        if (!issues.empty())
        {
            selection.is_valid = false;
            selection.diagnostics.add(*make_field_diagnostic(
                ProjectCategory::ProjectConfigInvalid, kSelectionSubject,
                std::string_view("顶层是 {name, plugins}，每项是 {name, linkage_type, options}"), issues,
                std::string_view("按 skills/id-kernel/SKILL.md 第 5 节与 docs/id-project-flow-spec.md 第 4 节修正该文件")));
            return selection;
        }

        // 同一插件出现两次：保留首次出现的顺序用于诊断定位，不择一
        for (std::size_t left = 0; left < selection.plugins.size(); ++left)
        {
            for (std::size_t right = left + 1; right < selection.plugins.size(); ++right)
            {
                if (selection.plugins[left].name != selection.plugins[right].name)
                {
                    continue;
                }
                selection.diagnostics.add(ProjectDiagnostic{
                    ProjectCategory::PluginDuplicate,
                    selection.plugins[left].name,
                    std::string("同一插件在选择文件内只出现一次"),
                    "第 " + to_decimal(left) + " 项与第 " + to_decimal(right) + " 项重名",
                    std::string(kSelectionSubject) + ".plugins",
                    std::string("删除重复项后重跑")});
            }
        }

        selection.is_valid = selection.diagnostics.is_empty();
        return selection;
    }

    std::string serialize_selection(std::string_view project_name, std::span<const SelectionPlugin> plugins)
    {
        JsonEmitter emitter;
        emitter.begin_object();
        emitter.key("name");
        emitter.append_string(project_name);
        emitter.key("plugins");
        emitter.begin_array();
        for (const SelectionPlugin& plugin : plugins)
        {
            emitter.begin_object();
            emitter.key("name");
            emitter.append_string(plugin.name);
            emitter.key("linkage_type");
            emitter.append_string(to_string(plugin.linkage));
            emitter.key("options");

            // 选项键按名字字节序写出，与书写顺序无关
            std::vector<std::string_view> names;
            names.reserve(plugin.options.size());
            for (const OptionAssignment& assignment : plugin.options)
            {
                names.push_back(assignment.name);
            }
            std::sort(names.begin(), names.end());

            emitter.begin_object();
            for (const std::string_view name : names)
            {
                for (const OptionAssignment& assignment : plugin.options)
                {
                    if (assignment.name != name)
                    {
                        continue;
                    }
                    emitter.key(assignment.name);
                    emitter.append_string(assignment.value);
                    break;
                }
            }
            emitter.end_object();

            emitter.end_object();
        }
        emitter.end_array();
        emitter.end_object();
        return emitter.finish();
    }

    std::optional<ProjectDiagnostic> check_selection_name(const SelectionFile& selection,
                                                          std::string_view project_name)
    {
        if (!selection.is_present || selection.name == project_name)
        {
            return std::nullopt;
        }
        return ProjectDiagnostic{ProjectCategory::ProjectNameMismatch,
                                std::string(project_name),
                                "选择文件的 name 与登记的项目名相同",
                                "选择文件写着 " + selection.name,
                                std::string(kSelectionSubject),
                                std::string("改正任一侧：修改 plugins.json 的 name，或改用对应的项目名")};
    }
} // namespace ID::Project
