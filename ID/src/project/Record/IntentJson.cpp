#include "project/Record/RecordJson.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "IDJsonParser.hpp"
#include "project/Base/Digest.hpp"
#include "project/Base/PathText.hpp"
#include "project/Base/Text.hpp"
#include "project/Record/RecordPaths.hpp"

/**
 *  `intent.json` 的确定性写出、读取回填与自校验。
 *
 *  `absent` 写成 JSON 的 null（不是空串），因为空串与"未计算"必须可区分；`payload_type` 与
 *  `fragment`、`address_scope` 与 `kind` 的配对在自校验里逐项检查。
 */

namespace ID::Project
{
    namespace
    {
        constexpr std::string_view kSubject = "intent.json";

        // Engine 作用域的唯一合法目标：工程根的本机登记文件
        constexpr std::string_view kEngineRegisterPath = "ID/project.json";

        std::string_view command_text(IntentCommand command) noexcept
        {
            switch (command)
            {
                case IntentCommand::New:     return std::string_view("new");
                case IntentCommand::Update:  return std::string_view("update");
                case IntentCommand::Clean:   return std::string_view("clean");
                case IntentCommand::Recover: return std::string_view("recover");
            }
            return std::string_view("update");
        }

        bool parse_command(std::string_view text, IntentCommand& out) noexcept
        {
            if (text == "new")
            {
                out = IntentCommand::New;
                return true;
            }
            if (text == "update")
            {
                out = IntentCommand::Update;
                return true;
            }
            if (text == "clean")
            {
                out = IntentCommand::Clean;
                return true;
            }
            if (text == "recover")
            {
                out = IntentCommand::Recover;
                return true;
            }
            return false;
        }

        std::string_view kind_text(OperationKind kind) noexcept
        {
            switch (kind)
            {
                case OperationKind::Mkdir:          return std::string_view("mkdir");
                case OperationKind::Write:          return std::string_view("write");
                case OperationKind::Delete:         return std::string_view("delete");
                case OperationKind::Backup:         return std::string_view("backup");
                case OperationKind::Payload:        return std::string_view("payload");
                case OperationKind::RegisterWrite:  return std::string_view("register_write");
            }
            return std::string_view("write");
        }

        bool parse_kind(std::string_view text, OperationKind& out) noexcept
        {
            if (text == "mkdir")
            {
                out = OperationKind::Mkdir;
                return true;
            }
            if (text == "write")
            {
                out = OperationKind::Write;
                return true;
            }
            if (text == "delete")
            {
                out = OperationKind::Delete;
                return true;
            }
            if (text == "backup")
            {
                out = OperationKind::Backup;
                return true;
            }
            if (text == "payload")
            {
                out = OperationKind::Payload;
                return true;
            }
            if (text == "register_write")
            {
                out = OperationKind::RegisterWrite;
                return true;
            }
            return false;
        }

        std::string_view scope_text(AddressScope scope) noexcept
        {
            return scope == AddressScope::Engine ? std::string_view("engine") : std::string_view("project");
        }

        bool parse_scope(std::string_view text, AddressScope& out) noexcept
        {
            if (text == "project")
            {
                out = AddressScope::Project;
                return true;
            }
            if (text == "engine")
            {
                out = AddressScope::Engine;
                return true;
            }
            return false;
        }

        std::string_view payload_type_text(PayloadType type) noexcept
        {
            return type == PayloadType::Fragment ? std::string_view("fragment") : std::string_view("file");
        }

        bool parse_payload_type(std::string_view text, PayloadType& out) noexcept
        {
            if (text == "file")
            {
                out = PayloadType::File;
                return true;
            }
            if (text == "fragment")
            {
                out = PayloadType::Fragment;
                return true;
            }
            return false;
        }

        std::string_view status_text(OperationStatus status) noexcept
        {
            switch (status)
            {
                case OperationStatus::Pending: return std::string_view("pending");
                case OperationStatus::Done:    return std::string_view("done");
                case OperationStatus::Failed:  return std::string_view("failed");
            }
            return std::string_view("pending");
        }

        bool parse_status(std::string_view text, OperationStatus& out) noexcept
        {
            if (text == "pending")
            {
                out = OperationStatus::Pending;
                return true;
            }
            if (text == "done")
            {
                out = OperationStatus::Done;
                return true;
            }
            if (text == "failed")
            {
                out = OperationStatus::Failed;
                return true;
            }
            return false;
        }

        void write_digest_state(JsonEmitter& emitter, std::string_view key, const DigestState& state)
        {
            emitter.key(key);
            if (state.is_absent)
            {
                emitter.append_null();
                return;
            }
            emitter.append_string(state.hex);
        }

        void read_digest_state(JsonObjectReader& reader, std::string_view key, DigestState& out)
        {
            std::optional<std::string> text;
            if (!reader.read_required_nullable_string(key, text))
            {
                return;
            }
            if (!text.has_value())
            {
                out.is_absent = true;
                out.hex.clear();
                return;
            }
            out.is_absent = false;
            out.hex       = std::move(*text);
        }

        void write_snapshot(JsonEmitter& emitter, std::string_view key, const RecordSnapshot& snapshot)
        {
            emitter.key(key);
            emitter.begin_object();
            emitter.key("is_present");
            emitter.append_bool(snapshot.is_present);
            emitter.key("bytes");
            emitter.append_string(snapshot.bytes);
            emitter.key("digest");
            emitter.append_string(snapshot.digest);
            emitter.end_object();
        }

    } // namespace

    std::string serialize_intent(const IntentRecord& record)
    {
        JsonEmitter emitter;
        emitter.begin_object();

        emitter.key("attempt");
        emitter.append_string(record.attempt);
        emitter.key("schema_version");
        emitter.append_string(record.schema_version);
        emitter.key("rules_version");
        emitter.append_string(record.rules_version);
        emitter.key("command");
        emitter.append_string(command_text(record.command));
        emitter.key("generation_id");
        emitter.append_string(record.generation_id);
        emitter.key("selection_digest");
        emitter.append_string(record.selection_digest);
        emitter.key("plan_digest");
        emitter.append_string(record.plan_digest.to_hex());

        write_snapshot(emitter, "base_resolved", record.base_resolved);
        write_snapshot(emitter, "base_manifest", record.base_manifest);
        emitter.key("has_base_commit");
        emitter.append_bool(record.has_base_commit);
        write_snapshot(emitter, "target_resolved", record.target_resolved);
        write_snapshot(emitter, "target_manifest", record.target_manifest);

        emitter.key("operations");
        emitter.begin_array();
        for (const IntentOperation& operation : record.operations)
        {
            emitter.begin_object();
            emitter.key("id");
            emitter.append_string(operation.id);
            emitter.key("kind");
            emitter.append_string(kind_text(operation.kind));
            emitter.key("address_scope");
            emitter.append_string(scope_text(operation.address_scope));
            emitter.key("path");
            emitter.append_string(operation.path);
            emitter.key("fragment");
            if (operation.fragment.has_value())
            {
                emitter.append_string(*operation.fragment);
            }
            else
            {
                emitter.append_null();
            }
            emitter.key("payload_type");
            emitter.append_string(payload_type_text(operation.payload_type));
            write_digest_state(emitter, "before", operation.before);
            write_digest_state(emitter, "after", operation.after);
            emitter.key("payload");
            emitter.append_string(operation.payload);
            emitter.key("backup");
            emitter.append_string(operation.backup);
            emitter.key("status");
            emitter.append_string(status_text(operation.status));
            emitter.key("actual");
            emitter.append_string(operation.actual);
            emitter.end_object();
        }
        emitter.end_array();

        emitter.key("created_dirs");
        emitter.begin_array();
        for (const std::string& directory : record.created_dirs)
        {
            emitter.append_string(directory);
        }
        emitter.end_array();

        emitter.key("removed_dirs");
        emitter.begin_array();
        for (const std::string& directory : record.removed_dirs)
        {
            emitter.append_string(directory);
        }
        emitter.end_array();

        emitter.key("backups");
        emitter.begin_array();
        for (const std::string& backup : record.backups)
        {
            emitter.append_string(backup);
        }
        emitter.end_array();

        emitter.key("phase");
        emitter.append_string(record.phase);

        emitter.key("next_argv");
        emitter.begin_array();
        for (const std::string& argument : record.next_argv)
        {
            emitter.append_string(argument);
        }
        emitter.end_array();

        emitter.end_object();
        return emitter.finish();
    }

    RecordRead<IntentRecord> parse_intent(std::string_view bytes)
    {
        RecordRead<IntentRecord> result;
        std::vector<FieldIssue> issues;

        ID::Json::JsonDocument document;
        const ID::Json::JsonValue* root = parse_json_root(document, bytes, kSubject, issues);
        if (root == nullptr)
        {
            result.diagnostic = make_record_diagnostic(kSubject, issues);
            return result;
        }

        IntentRecord& record = result.value;
        JsonObjectReader reader(*root, kSubject, issues);
        reader.read_string("attempt", record.attempt);
        reader.read_string("schema_version", record.schema_version);
        reader.read_string("rules_version", record.rules_version);
        reader.read_string("generation_id", record.generation_id);
        reader.read_string("selection_digest", record.selection_digest);
        reader.read_bool("has_base_commit", record.has_base_commit);
        reader.read_string("phase", record.phase);
        reader.read_string_array("created_dirs", record.created_dirs);
        reader.read_string_array("removed_dirs", record.removed_dirs);
        reader.read_string_array("backups", record.backups);
        reader.read_string_array("next_argv", record.next_argv);

        std::string command;
        if (reader.read_string("command", command))
        {
            IntentCommand parsed = IntentCommand::Update;
            if (!parse_command(command, parsed))
            {
                reader.issue("command", "\"new\" / \"update\" / \"clean\" / \"recover\"", command);
            }
            record.command = parsed;
        }

        std::string plan_digest;
        if (reader.read_string("plan_digest", plan_digest) && !digest_from_hex(plan_digest, record.plan_digest))
        {
            reader.issue("plan_digest", "64 位十六进制摘要", plan_digest);
        }

        if (const ID::Json::JsonValue* object = reader.read_object("base_resolved"))
        {
            JsonObjectReader snapshot_reader(*object, reader.child("base_resolved"), issues);
            snapshot_reader.read_bool("is_present", record.base_resolved.is_present);
            snapshot_reader.read_string("bytes", record.base_resolved.bytes);
            snapshot_reader.read_string("digest", record.base_resolved.digest);
        }
        if (const ID::Json::JsonValue* object = reader.read_object("base_manifest"))
        {
            JsonObjectReader snapshot_reader(*object, reader.child("base_manifest"), issues);
            snapshot_reader.read_bool("is_present", record.base_manifest.is_present);
            snapshot_reader.read_string("bytes", record.base_manifest.bytes);
            snapshot_reader.read_string("digest", record.base_manifest.digest);
        }
        if (const ID::Json::JsonValue* object = reader.read_object("target_resolved"))
        {
            JsonObjectReader snapshot_reader(*object, reader.child("target_resolved"), issues);
            snapshot_reader.read_bool("is_present", record.target_resolved.is_present);
            snapshot_reader.read_string("bytes", record.target_resolved.bytes);
            snapshot_reader.read_string("digest", record.target_resolved.digest);
        }
        if (const ID::Json::JsonValue* object = reader.read_object("target_manifest"))
        {
            JsonObjectReader snapshot_reader(*object, reader.child("target_manifest"), issues);
            snapshot_reader.read_bool("is_present", record.target_manifest.is_present);
            snapshot_reader.read_string("bytes", record.target_manifest.bytes);
            snapshot_reader.read_string("digest", record.target_manifest.digest);
        }

        if (const ID::Json::JsonValue* operations = reader.read_array("operations"))
        {
            record.operations.reserve(operations->size());
            for (std::size_t index = 0; index < operations->size(); ++index)
            {
                JsonObjectReader operation_reader((*operations)[index], reader.index(index), issues);
                IntentOperation operation;
                operation_reader.read_string("id", operation.id);
                operation_reader.read_string("path", operation.path);
                operation_reader.read_required_nullable_string("fragment", operation.fragment);
                operation_reader.read_string("payload", operation.payload);
                operation_reader.read_string("backup", operation.backup);
                operation_reader.read_string("actual", operation.actual);
                read_digest_state(operation_reader, "before", operation.before);
                read_digest_state(operation_reader, "after", operation.after);

                std::string kind;
                if (operation_reader.read_string("kind", kind))
                {
                    OperationKind parsed = OperationKind::Write;
                    if (!parse_kind(kind, parsed))
                    {
                        operation_reader.issue("kind", "已知操作种类", kind);
                    }
                    operation.kind = parsed;
                }

                std::string scope;
                if (operation_reader.read_string("address_scope", scope))
                {
                    AddressScope parsed = AddressScope::Project;
                    if (!parse_scope(scope, parsed))
                    {
                        operation_reader.issue("address_scope", "\"project\" 或 \"engine\"", scope);
                    }
                    operation.address_scope = parsed;
                }

                std::string payload_type;
                if (operation_reader.read_string("payload_type", payload_type))
                {
                    PayloadType parsed = PayloadType::File;
                    if (!parse_payload_type(payload_type, parsed))
                    {
                        operation_reader.issue("payload_type", "\"file\" 或 \"fragment\"", payload_type);
                    }
                    operation.payload_type = parsed;
                }

                std::string status;
                if (operation_reader.read_string("status", status))
                {
                    OperationStatus parsed = OperationStatus::Pending;
                    if (!parse_status(status, parsed))
                    {
                        operation_reader.issue("status", "\"pending\" / \"done\" / \"failed\"", status);
                    }
                    operation.status = parsed;
                }

                record.operations.push_back(std::move(operation));
            }
        }

        if (!issues.empty())
        {
            result.is_ok      = false;
            result.value      = IntentRecord{};
            result.diagnostic = make_record_diagnostic(kSubject, issues);
            return result;
        }

        result.diagnostic = check_intent_self(result.value);
        result.is_ok      = !result.diagnostic.has_value();
        return result;
    }

    std::optional<ProjectDiagnostic> check_intent_self(const IntentRecord& record)
    {
        std::vector<FieldIssue> issues;

        if (record.attempt.empty())
        {
            issues.push_back(FieldIssue{"intent.attempt", "非空 attempt 标识", "空串"});
        }
        if (record.schema_version != kSchemaVersion)
        {
            issues.push_back(FieldIssue{"intent.schema_version", std::string(kSchemaVersion),
                                        record.schema_version});
        }
        if (record.rules_version.empty())
        {
            issues.push_back(FieldIssue{"intent.rules_version", "非空规则标识", "空串"});
        }
        if (!is_digest_hex(record.generation_id))
        {
            issues.push_back(FieldIssue{"intent.generation_id", "64 位十六进制摘要", record.generation_id});
        }
        if (!is_digest_hex(record.selection_digest))
        {
            issues.push_back(FieldIssue{"intent.selection_digest", "64 位十六进制摘要", record.selection_digest});
        }
        if (record.plan_digest.is_zero())
        {
            issues.push_back(FieldIssue{"intent.plan_digest", "已计算的非零摘要", "全零"});
        }

        std::vector<std::string> ids;
        for (std::size_t index = 0; index < record.operations.size(); ++index)
        {
            const std::string field = "intent.operations[" + to_decimal(index) + "]";
            const IntentOperation& operation = record.operations[index];

            if (operation.id.empty())
            {
                issues.push_back(FieldIssue{field + ".id", "非空稳定标识", "空串"});
            }
            for (const std::string& known : ids)
            {
                if (known == operation.id)
                {
                    issues.push_back(FieldIssue{field + ".id", "同一 intent 内 id 唯一",
                                                operation.id + " 重复"});
                }
            }
            ids.push_back(operation.id);

            const bool has_fragment = operation.fragment.has_value();
            if (operation.payload_type == PayloadType::File && has_fragment)
            {
                issues.push_back(FieldIssue{field + ".fragment", "payload_type = file 时必须为空",
                                            *operation.fragment});
            }
            if (operation.payload_type == PayloadType::Fragment && !has_fragment)
            {
                issues.push_back(FieldIssue{field + ".fragment", "payload_type = fragment 时必须非空",
                                            "缺失"});
            }

            if (operation.address_scope == AddressScope::Engine)
            {
                if (operation.kind != OperationKind::RegisterWrite)
                {
                    issues.push_back(FieldIssue{field + ".kind", "Engine 作用域只允许 register_write",
                                                std::string(kind_text(operation.kind))});
                }
                if (operation.path != kEngineRegisterPath)
                {
                    issues.push_back(FieldIssue{field + ".path", std::string(kEngineRegisterPath),
                                                operation.path});
                }
                if (has_fragment)
                {
                    issues.push_back(FieldIssue{field + ".fragment", "Engine 项无片段", "非空"});
                }
            }
            else
            {
                if (operation.kind == OperationKind::RegisterWrite)
                {
                    issues.push_back(FieldIssue{field + ".kind", "Project 作用域不允许 register_write",
                                                std::string(kind_text(operation.kind))});
                }
                if (operation.path.empty() || normalize_relative(operation.path).verdict != PathVerdict::Ok)
                {
                    issues.push_back(FieldIssue{field + ".path", "项目内相对路径",
                                                operation.path.empty() ? std::string("空串") : operation.path});
                }
            }

            if (!operation.before.is_absent && !is_digest_hex(operation.before.hex))
            {
                issues.push_back(FieldIssue{field + ".before", "null 或 64 位十六进制摘要", operation.before.hex});
            }
            if (!operation.after.is_absent && !is_digest_hex(operation.after.hex))
            {
                issues.push_back(FieldIssue{field + ".after", "null 或 64 位十六进制摘要", operation.after.hex});
            }
        }

        for (std::size_t index = 0; index < record.created_dirs.size(); ++index)
        {
            const std::string& directory = record.created_dirs[index];
            if (directory.empty() || normalize_relative(directory).verdict != PathVerdict::Ok)
            {
                issues.push_back(FieldIssue{"intent.created_dirs[" + to_decimal(index) + "]", "项目内相对路径",
                                            directory});
            }
        }

        return make_record_diagnostic(kSubject, issues);
    }
} // namespace ID::Project
