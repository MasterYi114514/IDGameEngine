#include "project/Record/RecordJson.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "IDJsonParser.hpp"
#include "project/Base/PathText.hpp"
#include "project/Base/Text.hpp"
#include "project/Record/RecordPaths.hpp"

/**
 *  `resolved.json` 的确定性写出、读取回填与自校验。
 *
 *  写出顺序即 docs/id/04 §4.4 的字段顺序（身份 → 输入 → 校验快照 → 选择 → 依赖边 → 顺序 →
 *  目标与变量 → 资产 → 保留集 → 限制），同一结构两次序列化逐字节相同。
 */

namespace ID::Project
{
    namespace
    {
        constexpr std::string_view kSubject = "resolved.json";

        // 限制段的首条固定值：模型缺口的公开告知，不是错误
        constexpr std::string_view kFirstLimitationCode = "g8_engine_option_not_projected";

        // 记录层只用文本取值集做自校验：U4 只依赖 U1，不复用 Resolve 单元的类型
        bool is_linkage_source_text(std::string_view text) noexcept
        {
            return text == "explicit" || text == "requirement";
        }

        bool is_option_source_text(std::string_view text) noexcept
        {
            return text == "explicit" || text == "default";
        }

        bool is_stage_text(std::string_view text) noexcept
        {
            return text == "copy" || text == "generate";
        }

        void append_string_array(JsonEmitter& emitter, std::string_view key, const std::vector<std::string>& values)
        {
            emitter.key(key);
            emitter.begin_array();
            for (const std::string& value : values)
            {
                emitter.append_string(value);
            }
            emitter.end_array();
        }

        void write_option_snapshot(JsonEmitter& emitter, const ResolvedOptionSnapshot& option)
        {
            emitter.begin_object();
            emitter.key("name");
            emitter.append_string(option.name);
            emitter.key("type");
            emitter.append_string(option.type);
            emitter.key("default");
            emitter.append_string(option.default_on ? "ON" : "OFF");
            emitter.key("description");
            emitter.append_string(option.description);
            emitter.key("defines");
            if (option.define.has_value())
            {
                emitter.append_string(*option.define);
            }
            else
            {
                emitter.append_null();
            }
            append_string_array(emitter, "links_when_on", option.links_when_on);
            emitter.end_object();
        }

        void write_manifest_snapshot(JsonEmitter& emitter, const ResolvedManifestSnapshot& snapshot)
        {
            emitter.begin_object();
            emitter.key("plugin");
            emitter.append_string(snapshot.plugin);
            emitter.key("manifest_digest");
            emitter.append_string(snapshot.manifest_digest);
            emitter.key("version");
            emitter.append_string(snapshot.version);
            append_string_array(emitter, "dependencies", snapshot.dependencies);
            append_string_array(emitter, "requirements", snapshot.requirements);

            emitter.key("options");
            emitter.begin_array();
            for (const ResolvedOptionSnapshot& option : snapshot.options)
            {
                write_option_snapshot(emitter, option);
            }
            emitter.end_array();
            emitter.end_object();
        }

        void write_selection_entry(JsonEmitter& emitter, const ResolvedSelectionEntry& entry)
        {
            emitter.begin_object();
            emitter.key("plugin");
            emitter.append_string(entry.plugin);
            emitter.key("linkage");
            emitter.append_string(entry.linkage);
            emitter.key("linkage_source");
            emitter.append_string(entry.linkage_source);
            emitter.key("is_direct");
            emitter.append_bool(entry.is_direct);
            append_string_array(emitter, "option_names", entry.option_names);
            append_string_array(emitter, "option_values", entry.option_values);
            append_string_array(emitter, "option_sources", entry.option_sources);
            emitter.end_object();
        }

        void write_target_entry(JsonEmitter& emitter, const ResolvedTargetEntry& target)
        {
            emitter.begin_object();
            emitter.key("plugin");
            emitter.append_string(target.plugin);
            emitter.key("target");
            emitter.append_string(target.target);
            emitter.key("target_type");
            emitter.append_string(target.target_type);
            append_string_array(emitter, "option_vars", target.option_vars);
            append_string_array(emitter, "option_defines", target.option_defines);
            emitter.key("static_macro");
            emitter.append_string(target.static_macro);
            emitter.key("export_macro");
            emitter.append_string(target.export_macro);
            append_string_array(emitter, "source_dirs", target.source_dirs);

            emitter.key("include_roots");
            emitter.begin_array();
            for (const ResolvedIncludeRoot& root : target.include_roots)
            {
                emitter.begin_object();
                emitter.key("path");
                emitter.append_string(root.path);
                emitter.key("is_system");
                emitter.append_bool(root.is_system);
                emitter.end_object();
            }
            emitter.end_array();

            emitter.key("vendor_links");
            emitter.begin_array();
            for (const ResolvedVendorLink& link : target.vendor_links)
            {
                emitter.begin_object();
                emitter.key("dir");
                emitter.append_string(link.dir);
                emitter.key("target");
                emitter.append_string(link.target);
                emitter.end_object();
            }
            emitter.end_array();
            emitter.end_object();
        }

        void write_asset_entry(JsonEmitter& emitter, const ResolvedAssetEntry& asset)
        {
            emitter.begin_object();
            emitter.key("path");
            emitter.append_string(asset.path);
            emitter.key("fragment");
            emitter.append_null();      // resolved 不记片段资产：写出恒为 null
            emitter.key("source");
            if (asset.source.has_value())
            {
                emitter.append_string(*asset.source);
            }
            else
            {
                emitter.append_null();
            }
            emitter.key("digest");
            emitter.append_string(asset.digest);
            emitter.key("stage");
            emitter.append_string(asset.stage);
            emitter.key("plugin");
            if (asset.plugin.has_value())
            {
                emitter.append_string(*asset.plugin);
            }
            else
            {
                emitter.append_null();
            }
            emitter.end_object();
        }

        // ---- 读取 ----
        void read_option_snapshot(const ID::Json::JsonValue& element, std::string_view path,
                                  std::vector<FieldIssue>& issues, ResolvedOptionSnapshot& out)
        {
            JsonObjectReader reader(element, path, issues);
            reader.read_string("name", out.name);
            reader.read_string("type", out.type);
            reader.read_required_nullable_string("defines", out.define);
            reader.read_string_array("links_when_on", out.links_when_on);

            std::string description;
            reader.read_string("description", description);
            out.description = std::move(description);

            std::string default_text;
            if (reader.read_string("default", default_text))
            {
                if (default_text != "ON" && default_text != "OFF")
                {
                    reader.issue("default", "\"ON\" 或 \"OFF\"", default_text);
                }
                out.default_on = default_text == "ON";
            }
        }

        void read_manifest_snapshot(const ID::Json::JsonValue& element, std::string_view path,
                                    std::vector<FieldIssue>& issues, ResolvedManifestSnapshot& out)
        {
            JsonObjectReader reader(element, path, issues);
            reader.read_string("plugin", out.plugin);
            reader.read_string("manifest_digest", out.manifest_digest);
            reader.read_string("version", out.version);
            reader.read_string_array("dependencies", out.dependencies);
            reader.read_string_array("requirements", out.requirements);

            const ID::Json::JsonValue* options = reader.read_array("options");
            if (options == nullptr)
            {
                return;
            }
            out.options.reserve(options->size());
            for (std::size_t index = 0; index < options->size(); ++index)
            {
                ResolvedOptionSnapshot option;
                read_option_snapshot((*options)[index], reader.index(index), issues, option);
                out.options.push_back(std::move(option));
            }
        }

        void read_selection_entry(const ID::Json::JsonValue& element, std::string_view path,
                                  std::vector<FieldIssue>& issues, ResolvedSelectionEntry& out)
        {
            JsonObjectReader reader(element, path, issues);
            reader.read_string("plugin", out.plugin);
            reader.read_string("linkage", out.linkage);
            reader.read_string("linkage_source", out.linkage_source);
            reader.read_bool("is_direct", out.is_direct);
            reader.read_string_array("option_names", out.option_names);
            reader.read_string_array("option_values", out.option_values);
            reader.read_string_array("option_sources", out.option_sources);
        }

        void read_target_entry(const ID::Json::JsonValue& element, std::string_view path,
                               std::vector<FieldIssue>& issues, ResolvedTargetEntry& out)
        {
            JsonObjectReader reader(element, path, issues);
            reader.read_string("plugin", out.plugin);
            reader.read_string("target", out.target);
            reader.read_string("target_type", out.target_type);
            reader.read_string_array("option_vars", out.option_vars);
            reader.read_string_array("option_defines", out.option_defines);
            reader.read_string("static_macro", out.static_macro);
            reader.read_string("export_macro", out.export_macro);
            reader.read_string_array("source_dirs", out.source_dirs);

            const ID::Json::JsonValue* roots = reader.read_array("include_roots");
            if (roots != nullptr)
            {
                out.include_roots.reserve(roots->size());
                for (std::size_t index = 0; index < roots->size(); ++index)
                {
                    JsonObjectReader root_reader((*roots)[index], reader.index(index), issues);
                    ResolvedIncludeRoot root;
                    root_reader.read_string("path", root.path);
                    root_reader.read_bool("is_system", root.is_system);
                    out.include_roots.push_back(std::move(root));
                }
            }

            const ID::Json::JsonValue* links = reader.read_array("vendor_links");
            if (links != nullptr)
            {
                out.vendor_links.reserve(links->size());
                for (std::size_t index = 0; index < links->size(); ++index)
                {
                    JsonObjectReader link_reader((*links)[index], reader.index(index), issues);
                    ResolvedVendorLink link;
                    link_reader.read_string("dir", link.dir);
                    link_reader.read_string("target", link.target);
                    out.vendor_links.push_back(std::move(link));
                }
            }
        }

        void read_asset_entry(const ID::Json::JsonValue& element, std::string_view path,
                              std::vector<FieldIssue>& issues, ResolvedAssetEntry& out)
        {
            JsonObjectReader reader(element, path, issues);
            reader.read_string("path", out.path);
            reader.read_required_nullable_string("source", out.source);
            reader.read_string("digest", out.digest);
            reader.read_required_nullable_string("plugin", out.plugin);
            reader.read_required_nullable_string("fragment", out.fragment);

            std::string stage;
            if (reader.read_string("stage", stage))
            {
                if (!is_stage_text(stage))
                {
                    reader.issue("stage", "\"copy\" 或 \"generate\"", stage);
                }
                out.stage = std::move(stage);
            }
        }
    } // namespace

    std::string serialize_resolved(const ResolvedRecord& record)
    {
        JsonEmitter emitter;
        emitter.begin_object();

        emitter.key("schema_version");
        emitter.append_string(record.schema_version);
        emitter.key("rules_version");
        emitter.append_string(record.rules_version);
        emitter.key("generation_id");
        emitter.append_string(record.generation_id);
        emitter.key("project_name");
        emitter.append_string(record.project_name);
        emitter.key("manifest_digest");
        emitter.append_string(record.manifest_digest);

        emitter.key("selection_digest");
        emitter.append_string(record.selection_digest);
        emitter.key("selection_bytes_digest");
        emitter.append_string(record.selection_bytes_digest);

        emitter.key("snapshot");
        emitter.begin_array();
        for (const ResolvedManifestSnapshot& snapshot : record.snapshot)
        {
            write_manifest_snapshot(emitter, snapshot);
        }
        emitter.end_array();

        emitter.key("selection");
        emitter.begin_array();
        for (const ResolvedSelectionEntry& entry : record.selection)
        {
            write_selection_entry(emitter, entry);
        }
        emitter.end_array();

        emitter.key("edges");
        emitter.begin_array();
        for (const ResolvedEdge& edge : record.edges)
        {
            emitter.begin_object();
            emitter.key("from");
            emitter.append_string(edge.from);
            emitter.key("to");
            emitter.append_string(edge.to);
            append_string_array(emitter, "controlling_options", edge.controlling_options);
            emitter.end_object();
        }
        emitter.end_array();

        append_string_array(emitter, "init_order", record.init_order);

        emitter.key("targets");
        emitter.begin_array();
        for (const ResolvedTargetEntry& target : record.targets)
        {
            write_target_entry(emitter, target);
        }
        emitter.end_array();

        emitter.key("assets");
        emitter.begin_array();
        for (const ResolvedAssetEntry& asset : record.assets)
        {
            write_asset_entry(emitter, asset);
        }
        emitter.end_array();

        append_string_array(emitter, "retention", record.retention);

        emitter.key("limitations");
        emitter.begin_array();
        for (const ResolvedLimitation& limitation : record.limitations)
        {
            emitter.begin_object();
            emitter.key("code");
            emitter.append_string(limitation.code);
            emitter.key("message");
            emitter.append_string(limitation.message);
            emitter.end_object();
        }
        emitter.end_array();

        emitter.end_object();
        return emitter.finish();
    }

    RecordRead<ResolvedRecord> parse_resolved(std::string_view bytes)
    {
        RecordRead<ResolvedRecord> result;
        std::vector<FieldIssue> issues;

        ID::Json::JsonDocument document;
        const ID::Json::JsonValue* root = parse_json_root(document, bytes, kSubject, issues);
        if (root == nullptr)
        {
            result.diagnostic = make_record_diagnostic(kSubject, issues);
            return result;
        }

        ResolvedRecord& record = result.value;
        JsonObjectReader reader(*root, kSubject, issues);
        reader.read_string("schema_version", record.schema_version);
        reader.read_string("rules_version", record.rules_version);
        reader.read_string("generation_id", record.generation_id);
        reader.read_string("project_name", record.project_name);
        reader.read_string("manifest_digest", record.manifest_digest);
        reader.read_string("selection_digest", record.selection_digest);
        reader.read_string("selection_bytes_digest", record.selection_bytes_digest);
        reader.read_string_array("init_order", record.init_order);
        reader.read_string_array("retention", record.retention);

        if (const ID::Json::JsonValue* snapshot = reader.read_array("snapshot"))
        {
            record.snapshot.reserve(snapshot->size());
            for (std::size_t index = 0; index < snapshot->size(); ++index)
            {
                ResolvedManifestSnapshot entry;
                read_manifest_snapshot((*snapshot)[index], reader.index(index), issues, entry);
                record.snapshot.push_back(std::move(entry));
            }
        }

        if (const ID::Json::JsonValue* selection = reader.read_array("selection"))
        {
            record.selection.reserve(selection->size());
            for (std::size_t index = 0; index < selection->size(); ++index)
            {
                ResolvedSelectionEntry entry;
                read_selection_entry((*selection)[index], reader.index(index), issues, entry);
                record.selection.push_back(std::move(entry));
            }
        }

        if (const ID::Json::JsonValue* edges = reader.read_array("edges"))
        {
            record.edges.reserve(edges->size());
            for (std::size_t index = 0; index < edges->size(); ++index)
            {
                JsonObjectReader edge_reader((*edges)[index], reader.index(index), issues);
                ResolvedEdge edge;
                edge_reader.read_string("from", edge.from);
                edge_reader.read_string("to", edge.to);
                edge_reader.read_string_array("controlling_options", edge.controlling_options);
                record.edges.push_back(std::move(edge));
            }
        }

        if (const ID::Json::JsonValue* targets = reader.read_array("targets"))
        {
            record.targets.reserve(targets->size());
            for (std::size_t index = 0; index < targets->size(); ++index)
            {
                ResolvedTargetEntry entry;
                read_target_entry((*targets)[index], reader.index(index), issues, entry);
                record.targets.push_back(std::move(entry));
            }
        }

        if (const ID::Json::JsonValue* assets = reader.read_array("assets"))
        {
            record.assets.reserve(assets->size());
            for (std::size_t index = 0; index < assets->size(); ++index)
            {
                ResolvedAssetEntry entry;
                read_asset_entry((*assets)[index], reader.index(index), issues, entry);
                record.assets.push_back(std::move(entry));
            }
        }

        if (const ID::Json::JsonValue* limitations = reader.read_array("limitations"))
        {
            record.limitations.reserve(limitations->size());
            for (std::size_t index = 0; index < limitations->size(); ++index)
            {
                JsonObjectReader limitation_reader((*limitations)[index], reader.index(index), issues);
                ResolvedLimitation limitation;
                limitation_reader.read_string("code", limitation.code);
                limitation_reader.read_string("message", limitation.message);
                record.limitations.push_back(std::move(limitation));
            }
        }

        if (!issues.empty())
        {
            result.is_ok      = false;
            result.value      = ResolvedRecord{};
            result.diagnostic = make_record_diagnostic(kSubject, issues);
            return result;
        }

        result.is_ok      = true;
        result.diagnostic = check_resolved_self(result.value);
        result.is_ok      = !result.diagnostic.has_value();
        return result;
    }

    std::optional<ProjectDiagnostic> check_resolved_self(const ResolvedRecord& record)
    {
        std::vector<FieldIssue> issues;
        auto check_hex = [&issues](std::string_view field, const std::string& value)
        {
            if (!is_digest_hex(value))
            {
                issues.push_back(FieldIssue{std::string(field), "64 位十六进制摘要",
                                            value.empty() ? std::string("空串") : value});
            }
        };
        auto check_relative = [&issues](std::string_view field, const std::string& value)
        {
            if (value.empty() || normalize_relative(value).verdict != PathVerdict::Ok)
            {
                issues.push_back(FieldIssue{std::string(field), "项目内相对路径（POSIX 分隔、不含 '..'）",
                                            value.empty() ? std::string("空串") : value});
            }
        };

        if (record.schema_version != kSchemaVersion)
        {
            issues.push_back(FieldIssue{"resolved.schema_version", std::string(kSchemaVersion),
                                        record.schema_version});
        }
        if (record.rules_version.empty())
        {
            issues.push_back(FieldIssue{"resolved.rules_version", "非空规则标识", "空串"});
        }
        check_hex("resolved.generation_id", record.generation_id);
        check_hex("resolved.manifest_digest", record.manifest_digest);
        check_hex("resolved.selection_digest", record.selection_digest);
        check_hex("resolved.selection_bytes_digest", record.selection_bytes_digest);

        // 限制段：首条固定值必须在，且是公开行为限制而不是错误
        if (record.limitations.empty() || record.limitations.front().code != kFirstLimitationCode)
        {
            issues.push_back(FieldIssue{"resolved.limitations[0].code", std::string(kFirstLimitationCode),
                                        record.limitations.empty() ? std::string("缺失")
                                                                   : record.limitations.front().code});
        }
        for (std::size_t index = 0; index < record.limitations.size(); ++index)
        {
            if (record.limitations[index].message.empty())
            {
                issues.push_back(FieldIssue{"resolved.limitations[" + to_decimal(index) + "].message",
                                            "非空说明", "空串"});
            }
        }

        // 保留集必须无条件含根 CMakeLists.txt（它不在任何资产项里）
        bool has_root_cmake = false;
        for (const std::string& path : record.retention)
        {
            if (path == kRootCmake)
            {
                has_root_cmake = true;
                break;
            }
        }
        if (!has_root_cmake)
        {
            issues.push_back(FieldIssue{"resolved.retention", "含根 " + std::string(kRootCmake) + " 路径",
                                        "缺失该路径"});
        }
        for (std::size_t index = 0; index < record.retention.size(); ++index)
        {
            check_relative("resolved.retention[" + to_decimal(index) + "]", record.retention[index]);
        }

        for (std::size_t index = 0; index < record.selection.size(); ++index)
        {
            const std::string field = "resolved.selection[" + to_decimal(index) + "]";
            const ResolvedSelectionEntry& entry = record.selection[index];
            if (entry.plugin.empty())
            {
                issues.push_back(FieldIssue{field + ".plugin", "非空插件名", "空串"});
            }
            if (entry.linkage != "static" && entry.linkage != "dynamic")
            {
                issues.push_back(FieldIssue{field + ".linkage", "\"static\" 或 \"dynamic\"", entry.linkage});
            }
            if (!is_linkage_source_text(entry.linkage_source))
            {
                issues.push_back(FieldIssue{field + ".linkage_source", "\"explicit\" 或 \"requirement\"",
                                            entry.linkage_source});
            }
            if (entry.option_names.size() != entry.option_values.size()
                || entry.option_names.size() != entry.option_sources.size())
            {
                issues.push_back(FieldIssue{field + ".option_names", "三个选项数组同长", "长度不一致"});
            }
            for (std::size_t option = 0; option < entry.option_sources.size(); ++option)
            {
                if (!is_option_source_text(entry.option_sources[option]))
                {
                    issues.push_back(FieldIssue{field + ".option_sources[" + to_decimal(option) + "]",
                                                "\"explicit\" 或 \"default\"", entry.option_sources[option]});
                }
            }
        }

        for (std::size_t index = 0; index < record.edges.size(); ++index)
        {
            const std::string field = "resolved.edges[" + to_decimal(index) + "]";
            if (record.edges[index].from.empty() || record.edges[index].to.empty())
            {
                issues.push_back(FieldIssue{field, "from/to 非空", "空串"});
            }
        }

        for (std::size_t index = 0; index < record.targets.size(); ++index)
        {
            const std::string field = "resolved.targets[" + to_decimal(index) + "]";
            const ResolvedTargetEntry& target = record.targets[index];
            if (target.target_type != "STATIC" && target.target_type != "SHARED")
            {
                issues.push_back(FieldIssue{field + ".target_type", "\"STATIC\" 或 \"SHARED\"",
                                            target.target_type});
            }
            for (std::size_t root = 0; root < target.include_roots.size(); ++root)
            {
                check_relative(field + ".include_roots[" + to_decimal(root) + "].path",
                               target.include_roots[root].path);
            }
        }

        for (std::size_t index = 0; index < record.assets.size(); ++index)
        {
            const std::string field = "resolved.assets[" + to_decimal(index) + "]";
            const ResolvedAssetEntry& asset = record.assets[index];
            check_relative(field + ".path", asset.path);
            check_hex(field + ".digest", asset.digest);
            if (!is_stage_text(asset.stage))
            {
                issues.push_back(FieldIssue{field + ".stage", "\"copy\" 或 \"generate\"", asset.stage});
            }
            if (asset.fragment.has_value())
            {
                issues.push_back(FieldIssue{field + ".fragment", "null（resolved 不记片段资产）",
                                            *asset.fragment});
            }
        }

        return make_record_diagnostic(kSubject, issues);
    }
} // namespace ID::Project
