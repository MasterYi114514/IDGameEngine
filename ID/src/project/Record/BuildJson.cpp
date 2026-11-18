#include "project/Record/RecordJson.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "IDJsonParser.hpp"
#include "project/Base/Text.hpp"

/**
 *  `build.json` 的确定性写出与读取回填。顶层是数组，顺序即发生顺序。
 *
 *  `configure_exit` / `build_exit` / `stage` / `log_path` 为 null 时表示"未执行或打不开"，不写 0；
 *  身份字段取不到时写 `is_present = false` 与 `reason`，不伪造取值。
 *
 *  `config` / `stage` / `verdict` 三个枚举文本在读取时按 docs/id/04 §4.8 的取值集校验，越界即
 *  record_invalid；三个字段互不推导（某阶段没启动时退出码为 null、`verdict` 照实写）。
 */

namespace ID::Project
{
    namespace
    {
        constexpr std::string_view kSubject = "build.json";

        // 取值集（docs/id/04 §4.8）：文档用小写 `debug`/`release`，与 spec §12.4 的 `Debug`/`Release` 分属两处口径
        constexpr std::string_view kConfigValues[]  = {"debug", "release"};
        constexpr std::string_view kStageValues[]   = {"configure", "build"};
        constexpr std::string_view kVerdictValues[] = {
            "success", "configure_failed", "build_failed", "invoke_failed",
            "artifact_missing", "identity_mismatch", "not_run",
        };

        bool is_in_value_set(std::string_view text, std::span<const std::string_view> allowed) noexcept
        {
            for (const std::string_view candidate : allowed)
            {
                if (candidate == text)
                {
                    return true;
                }
            }
            return false;
        }

        void write_identity_field(JsonEmitter& emitter, std::string_view key, const BuildIdentityField& field)
        {
            emitter.key(key);
            emitter.begin_object();
            emitter.key("is_present");
            emitter.append_bool(field.is_present);
            emitter.key("value");
            emitter.append_string(field.value);
            emitter.key("reason");
            emitter.append_string(field.reason);
            emitter.end_object();
        }

        void read_identity_field(JsonObjectReader& reader, std::vector<FieldIssue>& issues, std::string_view key,
                                 BuildIdentityField& out)
        {
            const ID::Json::JsonValue* object = reader.read_object(key);
            if (object == nullptr)
            {
                return;
            }
            JsonObjectReader field_reader(*object, reader.child(key), issues);
            field_reader.read_bool("is_present", out.is_present);
            field_reader.read_string("value", out.value);
            field_reader.read_string("reason", out.reason);
        }

        void write_string_array(JsonEmitter& emitter, std::string_view key, const std::vector<std::string>& values)
        {
            emitter.key(key);
            emitter.begin_array();
            for (const std::string& value : values)
            {
                emitter.append_string(value);
            }
            emitter.end_array();
        }

        void write_record(JsonEmitter& emitter, const BuildRecord& record)
        {
            emitter.begin_object();

            emitter.key("id");
            emitter.append_string(record.id);
            emitter.key("generation_id");
            emitter.append_string(record.generation_id);
            emitter.key("manifest_digest");
            emitter.append_string(record.manifest_digest);
            emitter.key("config");
            emitter.append_string(record.config);
            emitter.key("generator");
            emitter.append_string(record.generator);
            emitter.key("requested_toolchain");
            emitter.append_string(record.requested_toolchain);
            emitter.key("requested_arch");
            emitter.append_string(record.requested_arch);
            emitter.key("actual_toolchain");
            emitter.append_string(record.actual_toolchain);
            emitter.key("actual_arch");
            emitter.append_string(record.actual_arch);

            emitter.key("identity");
            emitter.begin_object();
            write_identity_field(emitter, "compiler_id", record.compiler_id);
            write_identity_field(emitter, "compiler_version", record.compiler_version);
            write_identity_field(emitter, "compiler_path", record.compiler_path);
            write_identity_field(emitter, "target_triple", record.target_triple);
            write_identity_field(emitter, "crt", record.crt);
            write_identity_field(emitter, "ipo", record.ipo);
            write_identity_field(emitter, "pointer_width", record.pointer_width);
            emitter.end_object();

            write_string_array(emitter, "configure_argv", record.configure_argv);
            write_string_array(emitter, "build_argv", record.build_argv);

            emitter.key("work_dir");
            emitter.append_string(record.work_dir);
            emitter.key("build_dir");
            emitter.append_string(record.build_dir);
            emitter.key("started_at");
            emitter.append_string(record.started_at);
            emitter.key("finished_at");
            emitter.append_string(record.finished_at);

            emitter.key("configure_exit");
            if (record.configure_exit.has_value())
            {
                emitter.append_int(*record.configure_exit);
            }
            else
            {
                emitter.append_null();
            }
            emitter.key("build_exit");
            if (record.build_exit.has_value())
            {
                emitter.append_int(*record.build_exit);
            }
            else
            {
                emitter.append_null();
            }
            emitter.key("stage");
            if (record.stage.has_value())
            {
                emitter.append_string(*record.stage);
            }
            else
            {
                emitter.append_null();
            }

            emitter.key("verdict");
            emitter.append_string(record.verdict);

            emitter.key("artifacts");
            emitter.begin_array();
            for (const BuildArtifact& artifact : record.artifacts)
            {
                emitter.begin_object();
                emitter.key("path");
                emitter.append_string(artifact.path);
                emitter.key("digest");
                emitter.append_string(artifact.digest);
                emitter.key("mtime");
                emitter.append_string(artifact.mtime);
                emitter.key("size_bytes");
                emitter.append_uint(artifact.size_bytes);
                emitter.end_object();
            }
            emitter.end_array();

            emitter.key("log_path");
            if (record.log_path.has_value())
            {
                emitter.append_string(*record.log_path);
            }
            else
            {
                emitter.append_null();
            }

            emitter.end_object();
        }

        void read_record(const ID::Json::JsonValue& element, std::string_view path, std::vector<FieldIssue>& issues,
                         BuildRecord& out)
        {
            JsonObjectReader reader(element, path, issues);
            reader.read_string("id", out.id);
            reader.read_string("generation_id", out.generation_id);
            reader.read_string("manifest_digest", out.manifest_digest);

            std::string config;
            if (reader.read_string("config", config))
            {
                if (!is_in_value_set(config, kConfigValues))
                {
                    reader.issue("config", "\"debug\" 或 \"release\"", config);
                }
                out.config = std::move(config);
            }

            reader.read_string("generator", out.generator);
            reader.read_string("requested_toolchain", out.requested_toolchain);
            reader.read_string("requested_arch", out.requested_arch);
            reader.read_string("actual_toolchain", out.actual_toolchain);
            reader.read_string("actual_arch", out.actual_arch);
            reader.read_string_array("configure_argv", out.configure_argv);
            reader.read_string_array("build_argv", out.build_argv);
            reader.read_string("work_dir", out.work_dir);
            reader.read_string("build_dir", out.build_dir);
            reader.read_string("started_at", out.started_at);
            reader.read_string("finished_at", out.finished_at);

            std::string verdict;
            if (reader.read_string("verdict", verdict))
            {
                if (!is_in_value_set(verdict, kVerdictValues))
                {
                    reader.issue("verdict",
                                 "success / configure_failed / build_failed / invoke_failed / "
                                 "artifact_missing / identity_mismatch / not_run",
                                 verdict);
                }
                out.verdict = std::move(verdict);
            }

            // 三个可空成员都是序列化必写项：键缺失即记录非法，显式 null 才是"未执行/打不开"
            reader.read_required_nullable_int("configure_exit", out.configure_exit);
            reader.read_required_nullable_int("build_exit", out.build_exit);

            std::optional<std::string> stage;
            if (reader.read_required_nullable_string("stage", stage))
            {
                if (stage.has_value() && !is_in_value_set(*stage, kStageValues))
                {
                    reader.issue("stage", "\"configure\" / \"build\" / null", *stage);
                }
                out.stage = std::move(stage);
            }
            reader.read_required_nullable_string("log_path", out.log_path);

            if (const ID::Json::JsonValue* identity = reader.read_object("identity"))
            {
                JsonObjectReader identity_reader(*identity, reader.child("identity"), issues);
                read_identity_field(identity_reader, issues, "compiler_id", out.compiler_id);
                read_identity_field(identity_reader, issues, "compiler_version", out.compiler_version);
                read_identity_field(identity_reader, issues, "compiler_path", out.compiler_path);
                read_identity_field(identity_reader, issues, "target_triple", out.target_triple);
                read_identity_field(identity_reader, issues, "crt", out.crt);
                read_identity_field(identity_reader, issues, "ipo", out.ipo);
                read_identity_field(identity_reader, issues, "pointer_width", out.pointer_width);
            }

            if (const ID::Json::JsonValue* artifacts = reader.read_array("artifacts"))
            {
                out.artifacts.reserve(artifacts->size());
                for (std::size_t index = 0; index < artifacts->size(); ++index)
                {
                    JsonObjectReader artifact_reader((*artifacts)[index], reader.index(index), issues);
                    BuildArtifact artifact;
                    artifact_reader.read_string("path", artifact.path);
                    artifact_reader.read_string("digest", artifact.digest);
                    artifact_reader.read_string("mtime", artifact.mtime);
                    artifact_reader.read_uint64("size_bytes", artifact.size_bytes);
                    out.artifacts.push_back(std::move(artifact));
                }
            }
        }
    } // namespace

    std::string serialize_build_log(const BuildLog& log)
    {
        JsonEmitter emitter;
        emitter.begin_array();
        for (const BuildRecord& record : log.records)
        {
            write_record(emitter, record);
        }
        emitter.end_array();
        return emitter.finish();
    }

    RecordRead<BuildLog> parse_build_log(std::string_view bytes)
    {
        RecordRead<BuildLog> result;
        std::vector<FieldIssue> issues;

        ID::Json::JsonDocument document;
        const ID::Json::JsonValue* root = parse_json_root(document, bytes, kSubject, issues);
        if (root != nullptr)
        {
            if (!root->is_array())
            {
                issues.push_back(FieldIssue{std::string(kSubject), "顶层数组",
                                            std::string(json_type_name(root->type()))});
            }
            else
            {
                result.value.records.reserve(root->size());
                for (std::size_t index = 0; index < root->size(); ++index)
                {
                    BuildRecord record;
                    read_record((*root)[index], std::string(kSubject) + "[" + to_decimal(index) + "]", issues, record);
                    result.value.records.push_back(std::move(record));
                }
            }
        }

        if (!issues.empty())
        {
            result.is_ok      = false;
            result.value      = BuildLog{};
            result.diagnostic = make_record_diagnostic(kSubject, issues);
            return result;
        }

        result.is_ok = true;
        return result;
    }
} // namespace ID::Project
