#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "IDJsonDocument.hpp"
#include "IDJsonValue.hpp"
#include "project/Base/Diagnostic.hpp"
#include "project/Record/BuildRecord.hpp"
#include "project/Record/IntentRecord.hpp"
#include "project/Record/ManifestRecord.hpp"
#include "project/Record/ResolvedRecord.hpp"

/**
 *  记录与输入的 JSON 文本工具：确定性发射器 + 读取适配（字段取值的类型检查与"缺字段"区分）。
 *
 *  分工（计划书 D1）：读用 `ID::Json::parse`（DOM 只有读能力），写用本文件的发射器。
 *  `IDJson` 没有 DOM 构造 API，因此记录文本不经过 `IDJson` 的序列化接口，也不在别处另写解析器。
 *
 *  不变量：
 *  1. 发射器只输出 4 空格缩进、键序即调用顺序、以 '\n' 结尾、无 BOM 的文本；同一结构两次序列化
 *     逐字节相同（不含时间戳、随机顺序、本机路径）。
 *  2. 字符串转义只做 JSON 必需项：`"`、`\` 与 < 0x20 的控制字符；UTF-8 字节原样写出，不做 \u 转义膨胀。
 *  3. 读取适配只回答"字段在不在、类型对不对"，业务校验（必填性之外的取值集与自洽性）在各自的
 *     parse_* 与 check_* 里。
 *  4. 一次读取要么全部字段就位（is_ok 为真），要么给出全部字段问题；不"取第一条"、不静默补默认值。
 *     失败类别固定 record_invalid（码 2），对象写明哪份记录的哪个字段。
 *
 *  离线工具、非运行时热路径：本模块不在逐帧或逐元素路径上，std::string / std::vector 的分配成本可接受，
 *  因此不做逐点分配失败检查——这是对 cpp-style 第 9 节「分配失败不得忽略」的显式例外
 *  （范围仅限容器与字符串分配），与 project/Base 的既有口径一致。
 */

namespace ID::Project
{
    /**
     *  确定性 JSON 文本发射器：按调用顺序写键，缩进与换行固定。
     *  纯输出侧工具，不参与解析；调用方负责给出与文档表格一致的键序。
     */
    class JsonEmitter
    {
    public:
        JsonEmitter() = default;

        // 打开一个对象；根值直接调用即可
        void begin_object();

        // 关闭当前对象；空对象写成 "{}"
        void end_object();

        // 打开一个数组；空数组写成 "[]"
        void begin_array();

        // 关闭当前数组
        void end_array();

        /**
         *  @brief 写一个成员键（必须在对象内，且紧随其后写一个值）
         *  @param name 键名，按 UTF-8 字节原样写出（不转义非必需字符）
         */
        void key(std::string_view name);

        void append_string(std::string_view value);
        void append_bool(bool value);
        void append_int(std::int64_t value);
        void append_uint(std::uint64_t value);
        void append_null();

        /**
         *  @brief 取输出文本
         *  @return 完整 JSON 文本，以 '\n' 结尾
         *  @note 只在所有容器都已关闭后调用；调用后对象内容被移出，不再可用
         */
        std::string finish();

    private:
        struct Frame
        {
            bool is_object = false;   // 对象帧为真，数组帧为假
            bool has_items = false;   // 是否已写过成员/元素（决定是否需要逗号）
        };

        void before_value();
        void append_indent(std::size_t depth);
        void append_quoted(std::string_view text);

        std::string              m_text              = {};
        std::vector<Frame>       m_frames            = {};
        bool                     m_is_awaiting_value = false;   // 刚写完键，等待其值
    };

    /**
     *  一个字段的读取问题：字段路径、期望与实际。字段路径形如 `resolved.selection[0].linkage`。
     */
    struct FieldIssue
    {
        std::string field    = {};
        std::string expected = {};
        std::string actual   = {};
    };

    /**
     *  @brief 解析 JSON 文本并取根值
     *  @param document 调用方持有的文档（不可移动），返回值指向它内部的根值
     *  @param bytes JSON 文本
     *  @param subject 诊断对象（记录名），用于失败时的 actual 文案
     *  @param issues 失败时追加一条语法问题
     *  @return 成功时返回根值指针；解析失败返回 nullptr
     *  @note 文档内字符串的生命周期到 document.reset()/析构，调用方必须在同一作用域内取完全部字段
     */
    const ID::Json::JsonValue* parse_json_root(ID::Json::JsonDocument& document, std::string_view bytes,
                                               std::string_view subject, std::vector<FieldIssue>& issues);

    /**
     *  @brief 取 JSON 类型的人读名（数组/对象/字符串/整数/布尔/null）
     */
    std::string_view json_type_name(ID::Json::JsonType type) noexcept;

    /**
     *  对象字段读取适配：路径带上下文，问题追加到调用方给的列表里。
     *  非对象载体上调用任何 read_* 都会记一条问题并返回失败，不访问越界内存。
     */
    class JsonObjectReader
    {
    public:
        JsonObjectReader(const ID::Json::JsonValue& object, std::string_view path,
                         std::vector<FieldIssue>& issues) noexcept;

        // 载体是否是对象
        bool is_object() const noexcept;

        // 按字节比较查成员；不存在返回 nullptr
        const ID::Json::JsonValue* find(std::string_view key) const noexcept;

        bool read_string(std::string_view key, std::string& out);

        // 可空成员的两种语义按"序列化是否必写"选择：
        //   read_optional_*：键可缺失，缺失与 null 同义，用于文本里可省略的成员；
        //   read_required_nullable_*：键必填、值可为 null，用于序列化恒写该项的成员——缺键即记录非法。
        bool read_optional_string(std::string_view key, std::optional<std::string>& out);
        bool read_required_nullable_string(std::string_view key, std::optional<std::string>& out);
        bool read_bool(std::string_view key, bool& out);
        bool read_int(std::string_view key, int& out);
        bool read_optional_int(std::string_view key, std::optional<int>& out);
        bool read_required_nullable_int(std::string_view key, std::optional<int>& out);
        bool read_uint64(std::string_view key, std::uint64_t& out);
        bool read_string_array(std::string_view key, std::vector<std::string>& out);
        bool read_optional_string_array(std::string_view key, std::vector<std::string>& out);

        // 必填对象/数组：缺失或类型不符时记问题并返回 nullptr
        const ID::Json::JsonValue* read_object(std::string_view key);
        const ID::Json::JsonValue* read_array(std::string_view key);

        // 子路径构造：对象成员与数组下标
        std::string child(std::string_view key) const;
        std::string index(std::size_t element) const;

        // 追加一条字段问题
        void issue(std::string_view key, std::string_view expected, std::string_view actual);

    private:
        const ID::Json::JsonValue*  m_object = nullptr;    // 不拥有；生命周期由文档保证
        std::string                 m_path   = {};
        std::vector<FieldIssue>*    m_issues = nullptr;
    };

    /**
     *  @brief 把字段问题汇总成一条诊断
     *  @param category 失败类别（记录用 record_invalid，输入文件用各自的输入类别）
     *  @param subject 诊断对象（记录名或文件相对路径）
     *  @param expected 期望的一句话说明
     *  @param issues 字段问题清单；为空时返回空 optional
     *  @param next_action 下一步可执行动作
     *  @return 第一条问题的字段作为定位，actual 列出全部问题（最多 4 条，其余计数量）
     */
    std::optional<ProjectDiagnostic> make_field_diagnostic(ProjectCategory category, std::string_view subject,
                                                           std::string_view expected,
                                                           const std::vector<FieldIssue>& issues,
                                                           std::string_view next_action);

    /**
     *  @brief 把字段问题汇总成一条 record_invalid 诊断
     *  @param subject 诊断对象（记录名）
     *  @param issues 字段问题清单；为空时返回空 optional
     */
    std::optional<ProjectDiagnostic> make_record_diagnostic(std::string_view subject,
                                                            const std::vector<FieldIssue>& issues);

    /**
     *  @brief 判断文本是否是 64 位小写/大写十六进制摘要
     *  @param text 待检查文本
     */
    bool is_digest_hex(std::string_view text) noexcept;

    /**
     *  一次记录读取的结果：is_ok 为假时 diagnostic 是 record_invalid（码 2）。
     */
    template <typename T>
    struct RecordRead
    {
        bool                             is_ok      = false;
        T                                value       = {};
        std::optional<ProjectDiagnostic> diagnostic  = {};
    };

    // ---- 四类记录的编解码与自校验 ----
    // 读取：解析失败、缺字段、类型不符、枚举文本不在取值集 → record_invalid，对象写明文件与字段
    // 写出：同一结构两次序列化逐字节相同；键序与 docs/id/04 的表格顺序一致

    RecordRead<ResolvedRecord> parse_resolved(std::string_view bytes);
    std::string serialize_resolved(const ResolvedRecord& record);

    RecordRead<ManifestRecord> parse_manifest_record(std::string_view bytes);
    std::string serialize_manifest(const ManifestRecord& record);

    RecordRead<IntentRecord> parse_intent(std::string_view bytes);
    std::string serialize_intent(const IntentRecord& record);

    RecordRead<BuildLog> parse_build_log(std::string_view bytes);
    std::string serialize_build_log(const BuildLog& log);

    /**
     *  @brief 只校验单份 `resolved` 的自洽性（写入前与读取后）
     *  @return 任一不符给 record_invalid（码 2），对象写明字段；自洽时为空
     *  @note 检查项：schema_version、规则标识非空、generation/摘要形态、`limitations` 首条固定值、
     *        保留集含根 `CMakeLists.txt`、选择条目的选项数组同长、边与目标条目的对象非空
     */
    std::optional<ProjectDiagnostic> check_resolved_self(const ResolvedRecord& record);

    /**
     *  @brief 只校验单份 `manifest` 的自洽性
     *  @return 任一不符给 record_invalid，对象写明字段
     *  @note 检查项：schema_version、条目数量与条目集合一致、`entries_digest` 与重算一致、
     *        每条 `generation_id` 等于记录顶层、条目身份不重复、摘要形态
     */
    std::optional<ProjectDiagnostic> check_manifest_self(const ManifestRecord& record);

    /**
     *  @brief 只校验单份 `intent` 的自洽性
     *  @return 任一不符给 record_invalid，对象写明字段
     *  @note 检查项：attempt/schema/规则标识非空、操作项 `id` 唯一、`payload_type` 与 `fragment` 一致、
     *        `address_scope` 与 `kind` 配对、Engine 项只能写 `ID/project.json` 且无片段
     */
    std::optional<ProjectDiagnostic> check_intent_self(const IntentRecord& record);

    /**
     *  @brief 两份记录之间的一致性判定
     *  @param resolved 已提交记录
     *  @param manifest 已提交清单记录
     *  @param manifest_bytes 实际写出的 manifest 字节（`resolved.manifest_digest` 的对象）
     *  @return 任一项不符给 record_invalid（2），对象写明"哪份记录的哪个字段"，不猜测哪一份可信
     *  @note 检查项：两份记录 schema_version 与 generation_id 相同；`resolved.manifest_digest` 等于
     *        manifest 字节摘要；`manifest.entry_count` 等于条目数；`entries_digest` 与重算一致；
     *        每个条目的 `generation_id` 等于记录顶层；条目身份不重复
     */
    std::optional<ProjectDiagnostic> check_records(const ResolvedRecord& resolved, const ManifestRecord& manifest,
                                                   std::string_view manifest_bytes);
} // namespace ID::Project
