#include "project/Input/Registry.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "project/Base/FileIo.hpp"
#include "project/Base/PathText.hpp"
#include "project/Base/Text.hpp"
#include "project/Record/RecordJson.hpp"

/**
 *  `project.json` 的读取、查找、单条合并写回与确定性序列化。
 *
 *  读取只做形态校验（对象数组、必填字段与类型、重复 name/path）；项目名与路径的语义合法性、
 *  路径重叠与目录状态由 P0 判定，本文件不重复。写回只合并本条记录，不重排、不改其它项目。
 */

namespace ID::Project
{
    namespace
    {
        // 登记文件的诊断对象名：固定文本，便于脚本与人读
        constexpr std::string_view kRegistrySubject = "project.json";

        ProjectDiagnostic make_registry_invalid(std::string expected, std::string actual, std::string location)
        {
            return ProjectDiagnostic{ProjectCategory::RegistryInvalid,
                                     std::string(kRegistrySubject),
                                     std::move(expected),
                                     std::move(actual),
                                     std::move(location),
                                     std::string("修正 <工程根>/ID/project.json 的该条记录后重跑")};
        }

        // 路径比较口径只有 PathText 一个入口：平台规则 + 8.3 短名展开（同一目录的不同大小写、别名与分隔符
        // 都不是两条登记，spec §2.1）
        bool same_path(std::string_view left, std::string_view right)
        {
            return is_same_absolute_path(left, right);
        }

        // 一条登记的形态：name/path 必须是非空字符串，is_initialized 必须是 bool
        bool read_registry_entry(const ID::Json::JsonValue& element, std::size_t index,
                                 std::vector<FieldIssue>& issues, RegistryEntry& out)
        {
            std::string path(kRegistrySubject);
            path += '[';
            path += to_decimal(index);
            path += ']';

            JsonObjectReader reader(element, path, issues);
            const bool is_ok = reader.read_string("name", out.name)
                            && reader.read_string("path", out.path)
                            && reader.read_bool("is_initialized", out.is_initialized);
            if (is_ok && (out.name.empty() || out.path.empty()))
            {
                reader.issue(std::string_view{}, "非空字符串",
                             out.name.empty() ? std::string_view("name 为空") : std::string_view("path 为空"));
                return false;
            }
            return is_ok;
        }

        // 重复登记：同 name 或同 path 的每一对都列出，不按"取第一条"择一
        void append_duplicate_diagnostics(const std::vector<RegistryEntry>& entries, DiagnosticSet& diagnostics)
        {
            for (std::size_t left = 0; left < entries.size(); ++left)
            {
                for (std::size_t right = left + 1; right < entries.size(); ++right)
                {
                    const bool is_name_conflict = entries[left].name == entries[right].name;
                    const bool is_path_conflict = same_path(entries[left].path, entries[right].path);
                    if (!is_name_conflict && !is_path_conflict)
                    {
                        continue;
                    }

                    std::string actual = "记录 ";
                    actual += to_decimal(left);
                    actual += " 与记录 ";
                    actual += to_decimal(right);
                    actual += " 的";
                    actual += is_name_conflict ? "项目名" : "项目路径";
                    actual += "相同（";
                    actual += is_name_conflict ? entries[left].name : entries[left].path;
                    actual += "）";

                    std::string location = std::string(kRegistrySubject) + "[" + to_decimal(left) + "] 与 ["
                                         + to_decimal(right) + "]";
                    diagnostics.add(make_registry_invalid("项目名与项目路径在登记文件内各自唯一", std::move(actual),
                                                          std::move(location)));
                }
            }
        }
    } // namespace

    RegistryFile load_registry(const std::filesystem::path& path)
    {
        RegistryFile registry;
        const FileRead read = read_file(path);
        if (read.status == ReadStatus::NotFound)
        {
            // 文件不存在视为空表：这是正常状态，不是错误
            registry.is_present = false;
            registry.is_valid   = true;
            return registry;
        }

        registry.is_present = true;
        if (read.status == ReadStatus::Failed)
        {
            registry.is_valid = false;
            registry.diagnostics.add(make_registry_invalid("可读的登记文件", "读取失败", to_utf8(path)));
            return registry;
        }

        std::vector<FieldIssue> issues;
        ID::Json::JsonDocument  document;
        const ID::Json::JsonValue* root = parse_json_root(document, read.bytes, kRegistrySubject, issues);
        if (root != nullptr && !root->is_array())
        {
            issues.push_back(FieldIssue{std::string(kRegistrySubject), "object[]（顶层数组）",
                                        std::string(json_type_name(root->type()))});
        }

        std::vector<RegistryEntry> entries;
        if (root != nullptr && root->is_array())
        {
            entries.reserve(root->size());
            for (std::size_t index = 0; index < root->size(); ++index)
            {
                RegistryEntry entry;
                if (read_registry_entry((*root)[index], index, issues, entry))
                {
                    entries.push_back(std::move(entry));
                }
            }
        }

        registry.entries = std::move(entries);
        if (!issues.empty())
        {
            registry.is_valid = false;
            registry.diagnostics.add(*make_field_diagnostic(
                ProjectCategory::RegistryInvalid, kRegistrySubject,
                std::string_view("顶层是 {name, path, is_initialized} 构成的对象数组"), issues,
                std::string_view("修正 <工程根>/ID/project.json 的字段后重跑")));
            return registry;
        }

        append_duplicate_diagnostics(registry.entries, registry.diagnostics);
        registry.is_valid = registry.diagnostics.is_empty();
        return registry;
    }

    const RegistryEntry* find_by_name(const RegistryFile& registry, std::string_view name) noexcept
    {
        for (const RegistryEntry& entry : registry.entries)
        {
            if (entry.name == name)
            {
                return &entry;
            }
        }
        return nullptr;
    }

    const RegistryEntry* find_by_path(const RegistryFile& registry, std::string_view normalized_path)
    {
        for (const RegistryEntry& entry : registry.entries)
        {
            if (same_path(entry.path, normalized_path))
            {
                return &entry;
            }
        }
        return nullptr;
    }

    RegistryWriteOutcome write_registry_entry(const std::filesystem::path& registry_path,
                                              const RegistryEntry& entry,
                                              bool is_new_entry)
    {
        RegistryWriteOutcome outcome;

        const RegistryFile current = load_registry(registry_path);
        if (!current.is_valid)
        {
            if (!current.diagnostics.is_empty())
            {
                outcome.diagnostic = current.diagnostics.at(0);
            }
            else
            {
                outcome.diagnostic = make_registry_invalid("可信的登记文件", "登记不可读", to_utf8(registry_path));
            }
            return outcome;
        }

        // 写回前把 path 统一成展开后的形态：8.3 短名不进 project.json，同一目录不会以两种形态登记（spec §2.1）
        RegistryEntry merged = entry;
        merged.path          = expand_existing_prefixes(entry.path);

        std::vector<RegistryEntry> entries = current.entries;
        std::size_t match = entries.size();
        for (std::size_t index = 0; index < entries.size(); ++index)
        {
            if (entries[index].name == merged.name)
            {
                match = index;
                break;
            }
        }

        if (match != entries.size())
        {
            if (is_new_entry && !same_path(entries[match].path, merged.path))
            {
                // 新登记撞名：同名记录已指向其它路径，按 project_exists 拒绝，不覆盖既有登记
                outcome.diagnostic = ProjectDiagnostic{ProjectCategory::ProjectExists,
                                                       merged.name,
                                                       std::string("新登记的项目名未被其它记录占用"),
                                                       "已被路径 " + entries[match].path + " 登记",
                                                       to_utf8(registry_path),
                                                       std::string("改用其它项目名，或先修正该既有记录")};
                return outcome;
            }
            entries[match] = merged;
        }
        else
        {
            for (const RegistryEntry& existing : entries)
            {
                if (!same_path(existing.path, merged.path))
                {
                    continue;
                }
                outcome.diagnostic = ProjectDiagnostic{ProjectCategory::ProjectExists,
                                                       merged.name,
                                                       std::string("新登记的项目路径未被其它记录占用"),
                                                       "已被项目 " + existing.name + " 登记",
                                                       to_utf8(registry_path),
                                                       std::string("改用其它路径，或先修正该既有记录")};
                return outcome;
            }
            entries.push_back(merged);
        }

        WriteRequest request;
        request.target  = registry_path;
        request.bytes   = serialize_registry(entries);
        request.subject = to_utf8(registry_path);

        const WriteOutcome write = write_file_atomic(request);
        if (!write.is_ok)
        {
            outcome.diagnostic = write.diagnostic.has_value()
                                     ? write.diagnostic
                                     : std::optional<ProjectDiagnostic>(ProjectDiagnostic{
                                           ProjectCategory::WriteFailed, to_utf8(registry_path),
                                           std::string("登记文件可原子写入"), std::string("写入失败"), to_utf8(registry_path),
                                           std::string("检查目录权限与磁盘空间后重跑")});
            return outcome;
        }

        outcome.is_ok = true;
        return outcome;
    }

    std::string serialize_registry(const std::vector<RegistryEntry>& entries)
    {
        JsonEmitter emitter;
        emitter.begin_array();
        for (const RegistryEntry& entry : entries)
        {
            emitter.begin_object();
            emitter.key("name");
            emitter.append_string(entry.name);
            emitter.key("path");
            emitter.append_string(entry.path);
            emitter.key("is_initialized");
            emitter.append_bool(entry.is_initialized);
            emitter.end_object();
        }
        emitter.end_array();
        return emitter.finish();
    }
} // namespace ID::Project
