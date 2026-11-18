#include "project/Input/PluginManifest.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "project/Base/Digest.hpp"
#include "project/Base/Text.hpp"
#include "project/Record/RecordJson.hpp"

/**
 *  `plugin_config.json` 的逐字段校验。
 *
 *  只判定格式与清单内部自洽：条目的磁盘存在性、路径形态与越界、依赖是否闭合、TAG 推导与选项合并
 *  都在别处——路径形态违规属于"写入目标违规"，由 P1 的资产计划统一报 path_outside_project（spec §2.3、
 *  §13.2），不在这里降级成"清单非法"。
 *  失败一律产出一条 warn 级 plugin_config_invalid（actual 写明原始忽略原因），由扫描方把该插件
 *  移出可选列表，不改退出码。
 */

namespace ID::Project
{
    namespace
    {
        constexpr std::string_view kManifestSubject  = "plugin_config.json";
        constexpr std::string_view kPluginRootPrefix = "IDPlugin/";

        // 十项字段的允许键集合：其余一律按未知字段拒绝
        constexpr std::string_view kTopLevelKeys[]  = {
            "name", "dependencies", "requirements", "header_dirs", "common_headers",
            "source_dirs", "vendor_dirs", "shared_vendors", "files", "options",
        };
        constexpr std::string_view kVendorKeys[]    = {"dir", "target"};
        constexpr std::string_view kOptionKeys[]    = {
            "name", "type", "default", "description", "defines", "links_when_on",
        };

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

        // 选项名与宏名：^[A-Z][A-Z0-9_]*$
        bool is_upper_identifier(std::string_view text) noexcept
        {
            if (text.empty() || text.front() < 'A' || text.front() > 'Z')
            {
                return false;
            }
            for (const char raw : text)
            {
                const bool is_upper = raw >= 'A' && raw <= 'Z';
                const bool is_digit = raw >= '0' && raw <= '9';
                if (!is_upper && !is_digit && raw != '_')
                {
                    return false;
                }
            }
            return true;
        }

        // child 是否落在 ancestor 目录之下（按路径段比较，不做字符串前缀比较）
        bool is_under_entry(std::string_view child, std::string_view ancestor) noexcept
        {
            if (ancestor.empty() || child.size() <= ancestor.size() + 1)
            {
                return false;
            }
            if (child.compare(0, ancestor.size(), ancestor) != 0)
            {
                return false;
            }
            return child[ancestor.size()] == '/';
        }

        // 清单元数据读取：先按十项字段读，再统一做交叉校验
        void read_manifest_fields(const ID::Json::JsonValue& root, std::vector<FieldIssue>& issues,
                                  PluginManifest& manifest)
        {
            JsonObjectReader reader(root, kManifestSubject, issues);

            reader.read_string("name", manifest.name);
            reader.read_string_array("dependencies", manifest.dependencies);
            reader.read_string_array("common_headers", manifest.common_headers);
            reader.read_string_array("source_dirs", manifest.source_dirs);
            reader.read_string_array("vendor_dirs", manifest.vendor_dirs);
            reader.read_string_array("files", manifest.files);

            // requirements：取值集 {SHARED_ONLY, STATIC_ONLY}，最多 1 项；空数组 = 两种形态皆可
            std::vector<std::string> requirements;
            if (reader.read_string_array("requirements", requirements))
            {
                if (requirements.size() > 1)
                {
                    reader.issue("requirements", "最多 1 项",
                                 to_decimal(requirements.size()) + " 项");
                }
                else if (!requirements.empty() && !parse_requirement(requirements.front(), manifest.requirement))
                {
                    reader.issue("requirements", "\"SHARED_ONLY\" 或 \"STATIC_ONLY\"", requirements.front());
                }
            }

            // header_dirs：先读原文，条目种类在全部条目读完后再判定。
            // 条目路径的**形态与越界**不在这里判定：它属于"写入目标的形态违规"，按 spec §2.3、
            // §13.2 由 P1 的资产计划统一报 path_outside_project（绝对路径、'..'、盘符、ADS、保留名、
            // 空段都在那一处落类别），本单元只判清单结构（类型、未知字段、交叉自洽）。
            std::vector<std::string> header_paths;
            reader.read_string_array("header_dirs", header_paths);

            // shared_vendors：元素只有 dir 与 target 两个字段；dir 是目录名，target 为空表示只复制
            const ID::Json::JsonValue* vendors = reader.read_array("shared_vendors");
            if (vendors != nullptr)
            {
                manifest.shared_vendors.reserve(vendors->size());
                for (std::size_t index = 0; index < vendors->size(); ++index)
                {
                    const ID::Json::JsonValue& element = (*vendors)[index];
                    const std::string item_path = reader.child("shared_vendors") + "[" + to_decimal(index) + "]";
                    append_unknown_keys(element, kVendorKeys, item_path, issues);

                    JsonObjectReader vendor_reader(element, item_path, issues);
                    SharedVendor vendor;
                    vendor_reader.read_string("dir", vendor.dir);
                    vendor_reader.read_optional_string("target", vendor.target);

                    if (vendor.dir.empty() || vendor.dir == ".." || vendor.dir.find('/') != std::string::npos
                        || vendor.dir.find('\\') != std::string::npos)
                    {
                        vendor_reader.issue("dir", "不含分隔符且不是 \"..\" 的目录名", vendor.dir);
                    }
                    if (vendor.target.has_value() && vendor.target->empty())
                    {
                        vendor_reader.issue("target", "null 或非空目标名", "空串");
                    }
                    manifest.shared_vendors.push_back(std::move(vendor));
                }
            }

            // options：name/type/default/description 必填，defines 与 links_when_on 可选
            const ID::Json::JsonValue* options = reader.read_array("options");
            if (options != nullptr)
            {
                manifest.options.reserve(options->size());
                for (std::size_t index = 0; index < options->size(); ++index)
                {
                    const ID::Json::JsonValue& element = (*options)[index];
                    const std::string item_path = reader.child("options") + "[" + to_decimal(index) + "]";
                    append_unknown_keys(element, kOptionKeys, item_path, issues);

                    JsonObjectReader option_reader(element, item_path, issues);
                    ManifestOption option;
                    const bool has_name = option_reader.read_string("name", option.name);
                    option_reader.read_string("description", option.description);

                    std::string type;
                    if (option_reader.read_string("type", type))
                    {
                        option.is_bool = type == "bool";
                        if (!option.is_bool)
                        {
                            option_reader.issue("type", "\"bool\"（v1 只允许该取值）", type);
                        }
                    }

                    std::string default_text;
                    if (option_reader.read_string("default", default_text))
                    {
                        if (default_text != "ON" && default_text != "OFF")
                        {
                            option_reader.issue("default", "\"ON\" 或 \"OFF\"（大小写敏感）", default_text);
                        }
                        option.default_on = default_text == "ON";
                    }

                    option_reader.read_optional_string("defines", option.define);
                    option_reader.read_optional_string_array("links_when_on", option.links_when_on);

                    // 空名不在取值集内，与非法字符同样按正则失败报告；不因"读到了字符串"而豁免
                    if (has_name && !is_upper_identifier(option.name))
                    {
                        option_reader.issue("name", "^[A-Z][A-Z0-9_]*$", option.name);
                    }
                    if (option.define.has_value() && !is_upper_identifier(*option.define))
                    {
                        option_reader.issue("defines", "^[A-Z][A-Z0-9_]*$ 的宏名", *option.define);
                    }
                    manifest.options.push_back(std::move(option));
                }
            }

            // 交叉校验：名字、依赖自洽、选项重名、links_when_on ⊆ dependencies
            for (const std::string& dependency : manifest.dependencies)
            {
                if (dependency.empty())
                {
                    reader.issue("dependencies", "非空插件名", "空串");
                }
                else if (dependency == manifest.name)
                {
                    reader.issue("dependencies", "不含自身", manifest.name);
                }
                else if (dependency == "Common")
                {
                    reader.issue("dependencies", "不含 Common（Common 经 common_headers 表达）", dependency);
                }
            }

            for (std::size_t left = 0; left < manifest.options.size(); ++left)
            {
                for (std::size_t right = left + 1; right < manifest.options.size(); ++right)
                {
                    if (manifest.options[left].name == manifest.options[right].name)
                    {
                        reader.issue("options", "同清单内选项名唯一",
                                     manifest.options[left].name + " 重复");
                    }
                }
            }

            for (const ManifestOption& option : manifest.options)
            {
                for (const std::string& upstream : option.links_when_on)
                {
                    bool is_declared = false;
                    for (const std::string& dependency : manifest.dependencies)
                    {
                        if (dependency == upstream)
                        {
                            is_declared = true;
                            break;
                        }
                    }
                    if (!is_declared)
                    {
                        reader.issue("options", "links_when_on ⊆ dependencies",
                                     option.name + " 引用了未声明的 " + upstream);
                    }
                }
            }

            // header_dirs 条目种类：落在同一清单另一个条目之下的为单文件条目，其余为目录条目
            manifest.header_dirs.reserve(header_paths.size());
            for (const std::string& path : header_paths)
            {
                HeaderDirEntry entry;
                entry.path = path;
                entry.kind = HeaderDirKind::Directory;
                for (const std::string& candidate : header_paths)
                {
                    if (candidate != path && is_under_entry(path, candidate))
                    {
                        entry.kind = HeaderDirKind::File;
                        break;
                    }
                }
                manifest.header_dirs.push_back(std::move(entry));
            }
        }
    } // namespace

    ManifestParse parse_manifest(std::string_view bytes, std::string_view directory_name)
    {
        ManifestParse result;
        std::vector<FieldIssue> issues;

        ID::Json::JsonDocument document;
        const ID::Json::JsonValue* root = parse_json_root(document, bytes, kManifestSubject, issues);
        if (root == nullptr)
        {
            result.diagnostic = make_field_diagnostic(
                ProjectCategory::PluginConfigInvalid, kManifestSubject,
                std::string_view("合法的清单 JSON"), issues,
                std::string_view("由插件作者修正该清单后重跑；修正前该插件被忽略"));
            return result;
        }

        append_unknown_keys(*root, kTopLevelKeys, kManifestSubject, issues);
        read_manifest_fields(*root, issues, result.manifest);

        if (result.manifest.name != directory_name)
        {
            issues.push_back(FieldIssue{std::string(kManifestSubject) + ".name", "等于所在目录名",
                                        "清单写着 " + result.manifest.name + "，目录是 "
                                            + std::string(directory_name)});
        }

        if (!issues.empty())
        {
            result.is_ok    = false;
            result.manifest = PluginManifest{};
            result.diagnostic = make_field_diagnostic(
                ProjectCategory::PluginConfigInvalid, kManifestSubject,
                std::string_view("清单字段齐全、类型正确且内部自洽（skills/id-plugin/SKILL.md 第 4.2、4.3 节）"),
                issues,
                std::string_view("由插件作者修正该清单后重跑；修正前该插件被忽略，不改变退出码"));
            return result;
        }

        result.manifest.source_root     = std::string(kPluginRootPrefix) + std::string(directory_name);
        result.manifest.manifest_digest = digest_text(bytes).to_hex();
        result.is_ok = true;
        return result;
    }
} // namespace ID::Project
