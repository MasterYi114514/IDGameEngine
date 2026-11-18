#include "project/Record/RecordJson.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "IDJsonParser.hpp"
#include "project/Base/Digest.hpp"
#include "project/Base/Text.hpp"

namespace ID::Project
{
    namespace
    {
        // 每级缩进的空格数：记录文本一律 4 空格
        constexpr std::size_t kIndentWidth = 4;

        // 诊断里最多列出的字段问题条数：其余按数量汇总，避免一条诊断无限膨胀
        constexpr std::size_t kMaxListedIssues = 4;

        // 单个 C0 控制字符的转义尾部：'\u00XX'
        constexpr char kHexDigits[] = "0123456789abcdef";

        void append_control_escape(std::string& text, unsigned char byte)
        {
            text += "\\u00";
            text += kHexDigits[(byte >> 4) & 0x0Fu];
            text += kHexDigits[byte & 0x0Fu];
        }
    } // namespace

    void JsonEmitter::append_indent(std::size_t depth)
    {
        m_text.append(depth * kIndentWidth, ' ');
    }

    void JsonEmitter::append_quoted(std::string_view text)
    {
        m_text += '"';
        for (const char raw : text)
        {
            const auto byte = static_cast<unsigned char>(raw);
            switch (byte)
            {
                case '"':   m_text += "\\\"";   break;
                case '\\':  m_text += "\\\\";   break;
                case '\n':  m_text += "\\n";    break;
                case '\r':  m_text += "\\r";    break;
                case '\t':  m_text += "\\t";    break;
                case '\b':  m_text += "\\b";    break;
                case '\f':  m_text += "\\f";    break;
                default:
                    // JSON 只要求转义 < 0x20 的控制字符；UTF-8 字节原样写出，不做 \u 膨胀
                    if (byte < 0x20)
                    {
                        append_control_escape(m_text, byte);
                    }
                    else
                    {
                        m_text += raw;
                    }
                    break;
            }
        }
        m_text += '"';
    }

    void JsonEmitter::before_value()
    {
        if (m_is_awaiting_value)
        {
            // 键已经写出了缩进与前缀，值紧随其后
            m_is_awaiting_value = false;
            return;
        }
        if (m_frames.empty())
        {
            return;     // 根值：不带缩进与前缀
        }

        Frame& frame = m_frames.back();
        if (frame.has_items)
        {
            m_text += ",\n";
        }
        else
        {
            m_text += '\n';
        }
        frame.has_items = true;
        append_indent(m_frames.size());
    }

    void JsonEmitter::begin_object()
    {
        before_value();
        m_text += '{';
        m_frames.push_back(Frame{true, false});
    }

    void JsonEmitter::end_object()
    {
        if (m_frames.empty() || !m_frames.back().is_object)
        {
            return;     // 调用方违约：缺配对的 begin_object
        }

        const bool has_items = m_frames.back().has_items;
        m_frames.pop_back();
        if (has_items)
        {
            m_text += '\n';
            append_indent(m_frames.size());
        }
        m_text += '}';
    }

    void JsonEmitter::begin_array()
    {
        before_value();
        m_text += '[';
        m_frames.push_back(Frame{false, false});
    }

    void JsonEmitter::end_array()
    {
        if (m_frames.empty() || m_frames.back().is_object)
        {
            return;     // 调用方违约：缺配对的 begin_array
        }

        const bool has_items = m_frames.back().has_items;
        m_frames.pop_back();
        if (has_items)
        {
            m_text += '\n';
            append_indent(m_frames.size());
        }
        m_text += ']';
    }

    void JsonEmitter::key(std::string_view name)
    {
        if (m_frames.empty() || !m_frames.back().is_object)
        {
            return;     // 调用方违约：键只能写在对象内
        }

        Frame& frame = m_frames.back();
        if (frame.has_items)
        {
            m_text += ",\n";
        }
        else
        {
            m_text += '\n';
        }
        frame.has_items = true;
        append_indent(m_frames.size());
        append_quoted(name);
        m_text += ": ";
        m_is_awaiting_value = true;
    }

    void JsonEmitter::append_string(std::string_view value)
    {
        before_value();
        append_quoted(value);
    }

    void JsonEmitter::append_bool(bool value)
    {
        before_value();
        m_text += value ? "true" : "false";
    }

    void JsonEmitter::append_int(std::int64_t value)
    {
        before_value();
        m_text += to_signed_decimal(value);
    }

    void JsonEmitter::append_uint(std::uint64_t value)
    {
        before_value();
        m_text += to_decimal(value);
    }

    void JsonEmitter::append_null()
    {
        before_value();
        m_text += "null";
    }

    std::string JsonEmitter::finish()
    {
        if (m_text.empty() || m_text.back() != '\n')
        {
            m_text += '\n';
        }
        return std::move(m_text);
    }

    const ID::Json::JsonValue* parse_json_root(ID::Json::JsonDocument& document, std::string_view bytes,
                                               std::string_view subject, std::vector<FieldIssue>& issues)
    {
        ID::Json::JsonParseError error{};
        const ID::Json::JsonError code = ID::Json::parse(document, bytes, &error);
        if (code != ID::Json::JsonError::none)
        {
            std::string actual = ID::Json::to_string(code);
            actual += " (line ";
            actual += to_decimal(error.line);
            actual += ", column ";
            actual += to_decimal(error.column);
            actual += ')';

            issues.push_back(FieldIssue{std::string(subject), "合法 JSON 文本（RFC 8259）", std::move(actual)});
            return nullptr;
        }
        return &document.root();
    }

    std::string_view json_type_name(ID::Json::JsonType type) noexcept
    {
        switch (type)
        {
            case ID::Json::JsonType::Null:            return std::string_view("null");
            case ID::Json::JsonType::Bool:            return std::string_view("bool");
            case ID::Json::JsonType::Int:             return std::string_view("int");
            case ID::Json::JsonType::Double:          return std::string_view("double");
            case ID::Json::JsonType::InlineString:    return std::string_view("string");
            case ID::Json::JsonType::String:          return std::string_view("string");
            case ID::Json::JsonType::Array:           return std::string_view("array");
            case ID::Json::JsonType::Object:          return std::string_view("object");
        }
        return std::string_view("unknown");
    }

    JsonObjectReader::JsonObjectReader(const ID::Json::JsonValue& object, std::string_view path,
                                       std::vector<FieldIssue>& issues) noexcept
        : m_object(&object)
        , m_path(path)
        , m_issues(&issues)
    {
        if (!object.is_object())
        {
            issue(std::string_view{}, "object", json_type_name(object.type()));
        }
    }

    bool JsonObjectReader::is_object() const noexcept
    {
        return m_object != nullptr && m_object->is_object();
    }

    const ID::Json::JsonValue* JsonObjectReader::find(std::string_view key) const noexcept
    {
        if (!is_object())
        {
            return nullptr;
        }
        return m_object->find(key);
    }

    std::string JsonObjectReader::child(std::string_view key) const
    {
        std::string result = m_path;
        if (!result.empty())
        {
            result += '.';
        }
        result += key;
        return result;
    }

    std::string JsonObjectReader::index(std::size_t element) const
    {
        std::string result = m_path;
        result += '[';
        result += to_decimal(element);
        result += ']';
        return result;
    }

    void JsonObjectReader::issue(std::string_view key, std::string_view expected, std::string_view actual)
    {
        if (m_issues == nullptr)
        {
            return;
        }
        FieldIssue problem;
        problem.field    = key.empty() ? m_path : child(key);
        problem.expected = std::string(expected);
        problem.actual   = std::string(actual);
        m_issues->push_back(std::move(problem));
    }

    bool JsonObjectReader::read_string(std::string_view key, std::string& out)
    {
        const ID::Json::JsonValue* value = find(key);
        if (value == nullptr)
        {
            issue(key, "string", "缺失");
            return false;
        }
        if (!value->is_string())
        {
            issue(key, "string", json_type_name(value->type()));
            return false;
        }
        out.assign(value->as_string());
        return true;
    }

    bool JsonObjectReader::read_optional_string(std::string_view key, std::optional<std::string>& out)
    {
        const ID::Json::JsonValue* value = find(key);
        if (value == nullptr)
        {
            out = std::nullopt;
            return true;
        }
        if (value->is_null())
        {
            out = std::nullopt;
            return true;
        }
        if (!value->is_string())
        {
            issue(key, "string|null", json_type_name(value->type()));
            return false;
        }
        out = std::string(value->as_string());
        return true;
    }

    bool JsonObjectReader::read_required_nullable_string(std::string_view key, std::optional<std::string>& out)
    {
        const ID::Json::JsonValue* value = find(key);
        if (value == nullptr)
        {
            issue(key, "string|null", "缺失");
            return false;
        }
        if (value->is_null())
        {
            out = std::nullopt;
            return true;
        }
        if (!value->is_string())
        {
            issue(key, "string|null", json_type_name(value->type()));
            return false;
        }
        out = std::string(value->as_string());
        return true;
    }

    bool JsonObjectReader::read_bool(std::string_view key, bool& out)
    {
        const ID::Json::JsonValue* value = find(key);
        if (value == nullptr)
        {
            issue(key, "bool", "缺失");
            return false;
        }
        if (!value->is_bool())
        {
            issue(key, "bool", json_type_name(value->type()));
            return false;
        }
        out = value->as_bool();
        return true;
    }

    bool JsonObjectReader::read_int(std::string_view key, int& out)
    {
        const ID::Json::JsonValue* value = find(key);
        if (value == nullptr)
        {
            issue(key, "int", "缺失");
            return false;
        }
        if (!value->is_int())
        {
            issue(key, "int", json_type_name(value->type()));
            return false;
        }
        out = static_cast<int>(value->as_int64());
        return true;
    }

    bool JsonObjectReader::read_optional_int(std::string_view key, std::optional<int>& out)
    {
        const ID::Json::JsonValue* value = find(key);
        if (value == nullptr || value->is_null())
        {
            out = std::nullopt;
            return true;
        }
        if (!value->is_int())
        {
            issue(key, "int|null", json_type_name(value->type()));
            return false;
        }
        out = static_cast<int>(value->as_int64());
        return true;
    }

    bool JsonObjectReader::read_required_nullable_int(std::string_view key, std::optional<int>& out)
    {
        const ID::Json::JsonValue* value = find(key);
        if (value == nullptr)
        {
            issue(key, "int|null", "缺失");
            return false;
        }
        if (value->is_null())
        {
            out = std::nullopt;
            return true;
        }
        if (!value->is_int())
        {
            issue(key, "int|null", json_type_name(value->type()));
            return false;
        }
        out = static_cast<int>(value->as_int64());
        return true;
    }

    bool JsonObjectReader::read_uint64(std::string_view key, std::uint64_t& out)
    {
        const ID::Json::JsonValue* value = find(key);
        if (value == nullptr)
        {
            issue(key, "int", "缺失");
            return false;
        }
        if (!value->is_int() || value->as_int64() < 0)
        {
            issue(key, "非负整数", value->is_int() ? std::string_view("负数") : json_type_name(value->type()));
            return false;
        }
        out = static_cast<std::uint64_t>(value->as_int64());
        return true;
    }

    bool JsonObjectReader::read_string_array(std::string_view key, std::vector<std::string>& out)
    {
        const ID::Json::JsonValue* value = find(key);
        if (value == nullptr)
        {
            issue(key, "string[]", "缺失");
            return false;
        }
        if (!value->is_array())
        {
            issue(key, "string[]", json_type_name(value->type()));
            return false;
        }

        std::vector<std::string> result;
        result.reserve(value->size());
        for (std::size_t element = 0; element < value->size(); ++element)
        {
            const ID::Json::JsonValue& item = (*value)[element];
            if (!item.is_string())
            {
                issue(index(element), "string", json_type_name(item.type()));
                return false;
            }
            result.emplace_back(item.as_string());
        }
        out = std::move(result);
        return true;
    }

    bool JsonObjectReader::read_optional_string_array(std::string_view key, std::vector<std::string>& out)
    {
        if (find(key) == nullptr)
        {
            out.clear();
            return true;
        }
        return read_string_array(key, out);
    }

    const ID::Json::JsonValue* JsonObjectReader::read_object(std::string_view key)
    {
        const ID::Json::JsonValue* value = find(key);
        if (value == nullptr)
        {
            issue(key, "object", "缺失");
            return nullptr;
        }
        if (!value->is_object())
        {
            issue(key, "object", json_type_name(value->type()));
            return nullptr;
        }
        return value;
    }

    const ID::Json::JsonValue* JsonObjectReader::read_array(std::string_view key)
    {
        const ID::Json::JsonValue* value = find(key);
        if (value == nullptr)
        {
            issue(key, "array", "缺失");
            return nullptr;
        }
        if (!value->is_array())
        {
            issue(key, "array", json_type_name(value->type()));
            return nullptr;
        }
        return value;
    }

    std::optional<ProjectDiagnostic> make_field_diagnostic(ProjectCategory category, std::string_view subject,
                                                           std::string_view expected,
                                                           const std::vector<FieldIssue>& issues,
                                                           std::string_view next_action)
    {
        if (issues.empty())
        {
            return std::nullopt;
        }

        std::string actual;
        const std::size_t listed = issues.size() < kMaxListedIssues ? issues.size() : kMaxListedIssues;
        for (std::size_t index = 0; index < listed; ++index)
        {
            if (index != 0)
            {
                actual += "; ";
            }
            actual += issues[index].field;
            actual += " = ";
            actual += issues[index].actual;
            actual += "（期望 ";
            actual += issues[index].expected;
            actual += "）";
        }
        if (issues.size() > listed)
        {
            actual += "; 共 ";
            actual += to_decimal(issues.size());
            actual += " 项字段问题";
        }

        ProjectDiagnostic diagnostic{category,
                                     std::string(subject),
                                     std::string(expected),
                                     std::move(actual),
                                     issues.front().field,
                                     std::string(next_action)};
        return diagnostic;
    }

    std::optional<ProjectDiagnostic> make_record_diagnostic(std::string_view subject,
                                                            const std::vector<FieldIssue>& issues)
    {
        return make_field_diagnostic(ProjectCategory::RecordInvalid, subject,
                                     std::string_view("记录字段齐全、类型正确且取值在合法集内"), issues,
                                     std::string_view("按 docs/id/04 的字段表修正该记录，或重跑对应命令重新生成记录"));
    }

    bool is_digest_hex(std::string_view text) noexcept
    {
        if (text.size() != kDigestHexLength)
        {
            return false;
        }
        for (const char raw : text)
        {
            const bool is_digit = raw >= '0' && raw <= '9';
            const bool is_lower = raw >= 'a' && raw <= 'f';
            const bool is_upper = raw >= 'A' && raw <= 'F';
            if (!is_digit && !is_lower && !is_upper)
            {
                return false;
            }
        }
        return true;
    }

    std::optional<ProjectDiagnostic> check_records(const ResolvedRecord& resolved, const ManifestRecord& manifest,
                                                   std::string_view manifest_bytes)
    {
        std::vector<FieldIssue> issues;

        if (resolved.schema_version != manifest.schema_version)
        {
            issues.push_back(FieldIssue{"resolved.schema_version 与 manifest.schema_version", "两者相同",
                                        resolved.schema_version + " / " + manifest.schema_version});
        }
        if (resolved.generation_id != manifest.generation_id)
        {
            issues.push_back(FieldIssue{"resolved.generation_id 与 manifest.generation_id", "两者相同",
                                        resolved.generation_id + " / " + manifest.generation_id});
        }

        // manifest_digest 的对象是实际写出的 manifest 字节，不是条目集合摘要
        const std::string bytes_digest = digest_text(manifest_bytes).to_hex();
        if (resolved.manifest_digest != bytes_digest)
        {
            issues.push_back(FieldIssue{"resolved.manifest_digest", bytes_digest + "（manifest 字节摘要）",
                                        resolved.manifest_digest});
        }

        if (manifest.entry_count != static_cast<std::uint64_t>(manifest.entries.size()))
        {
            issues.push_back(FieldIssue{"manifest.entry_count", to_decimal(manifest.entries.size()),
                                        to_decimal(manifest.entry_count)});
        }

        const std::string entries_digest = compute_entries_digest(manifest.entries).to_hex();
        if (manifest.entries_digest != entries_digest)
        {
            issues.push_back(FieldIssue{"manifest.entries_digest", entries_digest, manifest.entries_digest});
        }

        for (std::size_t index = 0; index < manifest.entries.size(); ++index)
        {
            const std::string field = "manifest.entries[" + to_decimal(index) + "]";
            if (manifest.entries[index].generation_id != manifest.generation_id)
            {
                issues.push_back(FieldIssue{field + ".generation_id", manifest.generation_id,
                                            manifest.entries[index].generation_id});
            }
            for (std::size_t other = index + 1; other < manifest.entries.size(); ++other)
            {
                const bool is_duplicate = manifest.entries[index].path == manifest.entries[other].path
                                       && manifest.entries[index].fragment == manifest.entries[other].fragment;
                if (is_duplicate)
                {
                    issues.push_back(FieldIssue{field, "条目身份 (path, fragment) 唯一",
                                                "与 [" + to_decimal(other) + "] 重复"});
                }
            }
        }

        return make_record_diagnostic("resolved.json 与 manifest.json", issues);
    }
} // namespace ID::Project
