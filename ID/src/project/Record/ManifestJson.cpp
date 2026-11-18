#include "project/Record/RecordJson.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "IDJsonParser.hpp"
#include "project/Base/Digest.hpp"
#include "project/Base/Text.hpp"
#include "project/Record/RecordPaths.hpp"

/**
 *  `manifest.json` 的确定性写出、读取回填、自校验与条目集合摘要。
 *
 *  条目身份是 `(path, fragment)`；同一对出现两次即记录非法。片段摘要由调用方用
 *  `ID::CMake::fragment_range_digest` 算好后放进条目，本文件不做片段文本处理。
 */

namespace ID::Project
{
    namespace
    {
        constexpr std::string_view kSubject = "manifest.json";

        std::string_view kind_text(ManifestKind kind) noexcept
        {
            switch (kind)
            {
                case ManifestKind::Copied:    return std::string_view("copied");
                case ManifestKind::Generated: return std::string_view("generated");
                case ManifestKind::Seed:      return std::string_view("seed");
            }
            return std::string_view("copied");
        }

        bool parse_kind(std::string_view text, ManifestKind& out) noexcept
        {
            if (text == "copied")
            {
                out = ManifestKind::Copied;
                return true;
            }
            if (text == "generated")
            {
                out = ManifestKind::Generated;
                return true;
            }
            if (text == "seed")
            {
                out = ManifestKind::Seed;
                return true;
            }
            return false;
        }

        std::string_view stage_text(ManifestStage stage) noexcept
        {
            return stage == ManifestStage::Generate ? std::string_view("generate") : std::string_view("copy");
        }

        bool parse_stage(std::string_view text, ManifestStage& out) noexcept
        {
            if (text == "copy")
            {
                out = ManifestStage::Copy;
                return true;
            }
            if (text == "generate")
            {
                out = ManifestStage::Generate;
                return true;
            }
            return false;
        }

        // (path, fragment) 的排序键：fragment 为空的整文件条目排在同名片段条目之前
        bool is_before(const ManifestEntry& left, const ManifestEntry& right) noexcept
        {
            if (left.path != right.path)
            {
                return std::string_view(left.path) < std::string_view(right.path);
            }
            if (left.fragment.has_value() != right.fragment.has_value())
            {
                return !left.fragment.has_value();
            }
            if (!left.fragment.has_value())
            {
                return false;
            }
            return std::string_view(*left.fragment) < std::string_view(*right.fragment);
        }

        bool is_same_identity(const ManifestEntry& left, const ManifestEntry& right) noexcept
        {
            return left.path == right.path && left.fragment == right.fragment;
        }
    } // namespace

    std::string serialize_manifest(const ManifestRecord& record)
    {
        JsonEmitter emitter;
        emitter.begin_object();

        emitter.key("schema_version");
        emitter.append_string(record.schema_version);
        emitter.key("generation_id");
        emitter.append_string(record.generation_id);
        emitter.key("entry_count");
        emitter.append_uint(record.entry_count);
        emitter.key("entries_digest");
        emitter.append_string(record.entries_digest);

        emitter.key("entries");
        emitter.begin_array();
        for (const ManifestEntry& entry : record.entries)
        {
            emitter.begin_object();
            emitter.key("path");
            emitter.append_string(entry.path);
            emitter.key("fragment");
            if (entry.fragment.has_value())
            {
                emitter.append_string(*entry.fragment);
            }
            else
            {
                emitter.append_null();
            }
            emitter.key("kind");
            emitter.append_string(kind_text(entry.kind));
            emitter.key("plugin");
            if (entry.plugin.has_value())
            {
                emitter.append_string(*entry.plugin);
            }
            else
            {
                emitter.append_null();
            }
            emitter.key("source");
            if (entry.source.has_value())
            {
                emitter.append_string(*entry.source);
            }
            else
            {
                emitter.append_null();
            }
            emitter.key("digest");
            emitter.append_string(entry.digest);
            emitter.key("stage");
            emitter.append_string(stage_text(entry.stage));
            emitter.key("generation_id");
            emitter.append_string(entry.generation_id);
            emitter.end_object();
        }
        emitter.end_array();

        emitter.end_object();
        return emitter.finish();
    }

    RecordRead<ManifestRecord> parse_manifest_record(std::string_view bytes)
    {
        RecordRead<ManifestRecord> result;
        std::vector<FieldIssue> issues;

        ID::Json::JsonDocument document;
        const ID::Json::JsonValue* root = parse_json_root(document, bytes, kSubject, issues);
        if (root == nullptr)
        {
            result.diagnostic = make_record_diagnostic(kSubject, issues);
            return result;
        }

        ManifestRecord& record = result.value;
        JsonObjectReader reader(*root, kSubject, issues);
        reader.read_string("schema_version", record.schema_version);
        reader.read_string("generation_id", record.generation_id);
        reader.read_uint64("entry_count", record.entry_count);
        reader.read_string("entries_digest", record.entries_digest);

        if (const ID::Json::JsonValue* entries = reader.read_array("entries"))
        {
            record.entries.reserve(entries->size());
            for (std::size_t index = 0; index < entries->size(); ++index)
            {
                JsonObjectReader entry_reader((*entries)[index], reader.index(index), issues);
                ManifestEntry entry;
                entry_reader.read_string("path", entry.path);
                entry_reader.read_required_nullable_string("fragment", entry.fragment);
                entry_reader.read_required_nullable_string("plugin", entry.plugin);
                entry_reader.read_required_nullable_string("source", entry.source);
                entry_reader.read_string("digest", entry.digest);
                entry_reader.read_string("generation_id", entry.generation_id);

                std::string kind;
                if (entry_reader.read_string("kind", kind))
                {
                    ManifestKind parsed = ManifestKind::Copied;
                    if (!parse_kind(kind, parsed))
                    {
                        entry_reader.issue("kind", "\"copied\" / \"generated\" / \"seed\"", kind);
                    }
                    entry.kind = parsed;
                }

                std::string stage;
                if (entry_reader.read_string("stage", stage))
                {
                    ManifestStage parsed = ManifestStage::Copy;
                    if (!parse_stage(stage, parsed))
                    {
                        entry_reader.issue("stage", "\"copy\" 或 \"generate\"", stage);
                    }
                    entry.stage = parsed;
                }

                record.entries.push_back(std::move(entry));
            }
        }

        if (!issues.empty())
        {
            result.is_ok      = false;
            result.value      = ManifestRecord{};
            result.diagnostic = make_record_diagnostic(kSubject, issues);
            return result;
        }

        result.diagnostic = check_manifest_self(result.value);
        result.is_ok      = !result.diagnostic.has_value();
        return result;
    }

    Digest compute_entries_digest(std::span<const ManifestEntry> entries)
    {
        std::vector<const ManifestEntry*> sorted;
        sorted.reserve(entries.size());
        for (const ManifestEntry& entry : entries)
        {
            sorted.push_back(&entry);
        }
        std::sort(sorted.begin(), sorted.end(),
                  [](const ManifestEntry* left, const ManifestEntry* right) noexcept
                  {
                      return is_before(*left, *right);
                  });

        Sha256 hash;
        hash.update_tag("id-project-manifest-entries");
        for (const ManifestEntry* entry : sorted)
        {
            hash.update_tag("entry");
            hash.update_field(entry->path);
            if (entry->fragment.has_value())
            {
                hash.update_tag("fragment");
                hash.update_field(*entry->fragment);
            }
            else
            {
                hash.update_tag("file");
            }
            hash.update_field(kind_text(entry->kind));
            hash.update_field(entry->digest);
            hash.update_field(stage_text(entry->stage));
            hash.update_field(entry->plugin.has_value() ? *entry->plugin : std::string());
            hash.update_field(entry->source.has_value() ? *entry->source : std::string());
        }
        return hash.finish();
    }

    std::optional<ProjectDiagnostic> check_manifest_self(const ManifestRecord& record)
    {
        std::vector<FieldIssue> issues;

        if (record.schema_version != kSchemaVersion)
        {
            issues.push_back(FieldIssue{"manifest.schema_version", std::string(kSchemaVersion),
                                        record.schema_version});
        }
        if (!is_digest_hex(record.generation_id))
        {
            issues.push_back(FieldIssue{"manifest.generation_id", "64 位十六进制摘要", record.generation_id});
        }
        if (record.entry_count != static_cast<std::uint64_t>(record.entries.size()))
        {
            issues.push_back(FieldIssue{"manifest.entry_count", to_decimal(record.entries.size()),
                                        to_decimal(record.entry_count)});
        }

        const std::string expected_digest = compute_entries_digest(record.entries).to_hex();
        if (record.entries_digest != expected_digest)
        {
            issues.push_back(FieldIssue{"manifest.entries_digest", expected_digest, record.entries_digest});
        }

        for (std::size_t index = 0; index < record.entries.size(); ++index)
        {
            const std::string field = "manifest.entries[" + to_decimal(index) + "]";
            const ManifestEntry& entry = record.entries[index];
            if (entry.path.empty())
            {
                issues.push_back(FieldIssue{field + ".path", "非空相对路径", "空串"});
            }
            if (!is_digest_hex(entry.digest))
            {
                issues.push_back(FieldIssue{field + ".digest", "64 位十六进制摘要", entry.digest});
            }
            if (entry.generation_id != record.generation_id)
            {
                issues.push_back(FieldIssue{field + ".generation_id", record.generation_id, entry.generation_id});
            }
            for (std::size_t other = index + 1; other < record.entries.size(); ++other)
            {
                if (!is_same_identity(entry, record.entries[other]))
                {
                    continue;
                }
                issues.push_back(FieldIssue{field + " 与 [" + to_decimal(other) + "]", "条目身份 (path, fragment) 唯一",
                                            "重复条目"});
            }
        }

        return make_record_diagnostic(kSubject, issues);
    }
} // namespace ID::Project
