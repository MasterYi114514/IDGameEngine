#include "IDJson.hpp"
#include "IDLog.hpp"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "benchmark_support.hpp"
#include "idjson_testdata.hpp"

#if defined(_WIN32)
    #if !defined(WIN32_LEAN_AND_MEAN)
        #define WIN32_LEAN_AND_MEAN
    #endif
    #if !defined(NOMINMAX)
        #define NOMINMAX
    #endif
    #include <windows.h>
#endif

/**
 *  IDJson 正确性用例。
 *
 *  判定依据只有公开头与 `IDPlugin/IDJson/README.md`：错误码语义、位置字段含义、失败后的文档状态、
 *  序列化返回值与 NUL 规则、arena 统计口径（`Common/Arena/SegmentedArena.hpp`）全部按契约断言，
 *  不按实现猜测；`Detail::` 下的类型只在公开头已声明的范围内用于构造非法输入（见各用例的 detail）。
 *
 *  取证方式：
 *  - 结构等价：owning 与 non_owning 两种模式解析同一份文本，用独立的 DOM 遍历器
 *    （`idjson_testdata.hpp` 的 `Fingerprint`，不调用被测序列化器）产出规范化文本逐字节比较。
 *  - 位置断言：`offset` 是出错字节相对输入起点的偏移（0 起），`line`/`column` 从 1 起且按字节计；
 *    多行、CRLF、行尾截断三类样例逐条写明期望值。
 *  - 序列化：紧凑与缩进两种输出按逐字节期望比对；容量探测/不足/恰好/多一字节四种容量按契约分别断言；
 *    `double` 的 round-trip 用位模式比较（位相同才算保真）。
 *  - 段来源：计数分配器走公开的 `SegmentAllocFn`/`SegmentFreeFn` 入口，统计 alloc/free 对称与巨段归还；
 *    "达到 N 次后失败"的上限用于受控注入 `JsonError::out_of_memory`（不改被测实现）。
 *  - 错误日志桥：挂接 IDLog 后解析多行坏 JSON，读回文件断言源名/错误码/行/列，再 detach 断言零新增；
 *    `IDJSON_ENABLE_LOG=OFF` 的构建变体按 `BENCHMARK_IDJSON_LOG` 断言零日志与零日志文件。
 *  - 4 GiB 输入：用 `VirtualAlloc` 取得 ≥ 4 GiB + 1 的地址区间后直接调用 `parse`（实现按长度先行拒绝，
 *    不读取任何字节），失败时按 `Uncovered` 登记客观障碍。
 *
 *  纪律：
 *  - 每个用例自包含（自己造文档、自己建日志器），不依赖用例顺序；
 *  - 失败用 `Check` 记录并继续，不用断言宏终止进程；
 *  - 用例内不直接写 stdout（控制台与报告必须一致）。
 */

namespace
{
    using ID::Json::JsonDocument;
    using ID::Json::JsonError;
    using ID::Json::JsonParseError;
    using ID::Json::JsonType;
    using ID::Json::JsonValue;
    using ID::Json::JsonWriteError;
    using ID::Json::JsonWriteOptions;
    using ID::Json::parse;
    using ID::Json::parse_non_owning;
    using ID::Json::serialize;
    using ID::Bench::JsonFixture::DoubleBits;
    using ID::Bench::JsonFixture::Fingerprint;
    using ID::Bench::JsonFixture::IsInsideInput;

    // ==================== 文件与文本小工具 ====================

    void RemoveFileQuiet(const std::string& path) noexcept
    {
        std::error_code error;
        std::filesystem::remove(path, error);
    }

    bool ReadWholeFile(const std::string& path, std::string& out) noexcept
    {
        out.clear();
        std::FILE* file = std::fopen(path.c_str(), "rb");
        if (file == nullptr)
        {
            return false;
        }

        char        buffer[4096];
        std::size_t read = 0;
        while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
        {
            out.append(buffer, read);
        }

        std::fclose(file);
        return true;
    }

    std::size_t CountLines(const std::string& text) noexcept
    {
        std::size_t count = 0;
        for (const char symbol : text)
        {
            if (symbol == '\n')
            {
                ++count;
            }
        }
        if (!text.empty() && text.back() != '\n')
        {
            ++count;
        }
        return count;
    }

    /// @brief 是否存在某一行同时包含全部片段（跨行命中不算通过）
    bool AnyLineContainsAll(const std::string& text, const std::vector<std::string_view>& needles) noexcept
    {
        std::size_t begin = 0;
        while (begin < text.size())
        {
            std::size_t end = text.find('\n', begin);
            if (end == std::string::npos)
            {
                end = text.size();
            }

            const std::string_view line(text.data() + begin, end - begin);
            bool                 all_found = true;
            for (const std::string_view needle : needles)
            {
                if (line.find(needle) == std::string_view::npos)
                {
                    all_found = false;
                    break;
                }
            }
            if (all_found)
            {
                return true;
            }

            begin = end + 1;
        }
        return false;
    }

    std::string TextFromBytes(const unsigned char* bytes, std::size_t count) noexcept
    {
        return std::string(reinterpret_cast<const char*>(bytes), count);
    }

    /// @brief 字节序列的小写十六进制文本（失败信息里逐字节可比对，不依赖终端编码显示）
    std::string HexOf(const char* data, std::size_t length) noexcept
    {
        constexpr char kDigits[] = "0123456789abcdef";
        std::string    hex;
        hex.reserve(length * 2);
        for (std::size_t index = 0; index < length; ++index)
        {
            const auto byte = static_cast<unsigned char>(data[index]);
            hex.push_back(kDigits[byte >> 4]);
            hex.push_back(kDigits[byte & 0x0Fu]);
        }
        return hex;
    }

    /**
     *  @brief 按对象成员名取值；未命中时返回 `kNullValue` 而不是 nullptr
     *  @note 用例里连续取值时不引入空指针解引用：被测实现若有缺陷（本该命中的键未命中），
     *        断言会失败并继续，而不是把整个测试进程带走
     */
    const JsonValue& Child(const JsonValue& parent, std::string_view key) noexcept
    {
        const JsonValue* found = parent.find(key);
        return found != nullptr ? *found : ID::Json::kNullValue;
    }

    /// @brief 解析一段文本并断言错误码与偏移（offset），用于错误路径逐例覆盖
    void CheckParseErrorOffset(const char* name, const std::string& text, JsonError expected_code,
                              std::uint32_t expected_offset) noexcept
    {
        JsonDocument   document;
        JsonParseError error{};
        const JsonError code = parse(document, text, &error);

        char detail[224] = {};
        std::snprintf(detail, sizeof(detail), "期望 code=%s offset=%u；实际 code=%s offset=%u line=%u column=%u",
                      ID::Json::to_string(expected_code), expected_offset,
                      ID::Json::to_string(code), error.offset, error.line, error.column);
        ID::Bench::Check(code == expected_code && error.offset == expected_offset, name, detail);
        ID::Bench::Check(document.root().is_null(), std::string(name) + ".root_null",
                         "契约（README §6）：解析失败后文档被置空，root() 为 null 值");
    }

    /// @brief 解析一段文本并断言错误码与完整位置（offset/line/column）
    void CheckParseErrorPosition(const char* name, const std::string& text, JsonError expected_code,
                                 std::uint32_t expected_offset, std::uint32_t expected_line,
                                 std::uint32_t expected_column) noexcept
    {
        JsonDocument   document;
        JsonParseError error{};
        const JsonError code = parse(document, text, &error);

        char detail[256] = {};
        std::snprintf(detail, sizeof(detail),
                      "期望 code=%s offset=%u line=%u column=%u；实际 code=%s offset=%u line=%u column=%u",
                      ID::Json::to_string(expected_code), expected_offset, expected_line, expected_column,
                      ID::Json::to_string(code), error.offset, error.line, error.column);
        ID::Bench::Check(code == expected_code && error.offset == expected_offset && error.line == expected_line
                             && error.column == expected_column,
                         name, detail);
    }

    // ==================== 固定文本与期望字节 ====================

    // 转义全表：\" \\ \/ \b \f \n \r \t + \u0000 \u001f \u007f \u00e9 \u4e2d + 代理对 \ud83d\ude00
    constexpr const char* kEscapeText =
        "\"\\\"\\\\\\/\\b\\f\\n\\r\\t\\u0000\\u001f\\u007f\\u00e9\\u4e2d\\ud83d\\ude00\"";

    // 解码后应为 20 字节：0x22 0x5C 0x2F 08 0C 0A 0D 09 00 1F 7F + UTF-8（C3 A9 / E4 B8 AD / F0 9F 98 80）
    constexpr unsigned char kEscapeDecoded[] = {
        0x22, 0x5C, 0x2F, 0x08, 0x0C, 0x0A, 0x0D, 0x09, 0x00, 0x1F,
        0x7F, 0xC3, 0xA9, 0xE4, 0xB8, 0xAD, 0xF0, 0x9F, 0x98, 0x80,
    };

    // 同一内容的紧凑序列化结果（39 字节）：短转义对称，其余 < 0x20 写 \u00XX，≥ 0x20 原样透传
    constexpr unsigned char kEscapeSerialized[] = {
        0x22,                                  // "
        0x5C, 0x22,                            // \"
        0x5C, 0x5C,                            // 反斜杠本身（写成两个字节）
        0x2F,                                  // /（不转义）
        0x5C, 0x62,                            // \b
        0x5C, 0x66,                            // \f
        0x5C, 0x6E,                            // \n
        0x5C, 0x72,                            // \r
        0x5C, 0x74,                            // \t
        0x5C, 0x75, 0x30, 0x30, 0x30, 0x30,    // \u0000
        0x5C, 0x75, 0x30, 0x30, 0x31, 0x66,    // \u001f
        0x7F,                                  // 0x7F ≥ 0x20，原样透传
        0xC3, 0xA9,                            // U+00E9
        0xE4, 0xB8, 0xAD,                      // U+4E2D
        0xF0, 0x9F, 0x98, 0x80,                // U+1F600
        0x22,                                  // "
    };

    // ==================== C1/C2/C7：解析正例矩阵 ====================

    void CaseParsePositiveMatrix() noexcept
    {
        const std::string mixed =
            "{\"id\":7,\"ratio\":0.5,\"ok\":true,\"none\":null,\"empty\":\"\",\"s7\":\"1234567\","
            "\"s8\":\"12345678\",\"arr\":[1,-2,3.5,\"x\"],\"obj\":{\"k\":\"v\",\"nested\":{\"deep\":[[]]}}}";
        const std::string padded = " \t\r\n" + mixed + "\n\t ";
        const std::string empty_containers = "{\"a\":{},\"b\":[],\"c\":[{}],\"d\":{\"e\":[]}}";
        const std::string unicode_text =
            "{\"\xE4\xB8\xAD\xE6\x96\x87\":\"\xE5\x80\xBC\",\"list\":[\"\xC3\xA9\",\"\xF0\x9F\x98\x80\"]}";

        struct TextCase
        {
            const char* name;
            const std::string* text;
        };
        const TextCase cases[] = {
            {"positive.mixed", &mixed},
            {"positive.whitespace_padded", &padded},
            {"positive.empty_containers", &empty_containers},
            {"positive.utf8_passthrough", &unicode_text},
        };

        for (const TextCase& item : cases)
        {
            JsonDocument owning;
            JsonDocument non_owning;
            const JsonError owning_code = parse(owning, *item.text);
            const JsonError non_owning_code = parse_non_owning(non_owning, *item.text);

            ID::Bench::Check(owning_code == JsonError::none, std::string(item.name) + ".owning_ok",
                             "owning 解析：固定合法文档必须成功");
            ID::Bench::Check(non_owning_code == JsonError::none, std::string(item.name) + ".non_owning_ok",
                             "non_owning 解析：同一文档必须成功");

            const std::string owning_fingerprint = Fingerprint(owning.root());
            const std::string non_owning_fingerprint = Fingerprint(non_owning.root());
            ID::Bench::Check(!owning_fingerprint.empty() && owning_fingerprint == non_owning_fingerprint,
                             std::string(item.name) + ".mode_equivalence",
                             "两种所有权模式必须产出同一结构（独立遍历器指纹逐字节比较，浮点按位）");
        }

        // 结构细节（契约见 README §2.2 的访问器表）
        JsonDocument document;
        ID::Bench::Check(parse(document, mixed) == JsonError::none, "mixed.parse_ok", "固定混合文档解析成功");
        const JsonValue& root = document.root();

        ID::Bench::Check(root.is_object() && root.size() == 9, "mixed.root_object",
                         "根是对象且成员数为 9（id/ratio/ok/none/empty/s7/s8/arr/obj）");

        ID::Bench::Check(root.find("id") != nullptr && Child(root, "id").is_int()
                             && Child(root, "id").as_int64() == 7,
                         "mixed.id", "整数形态的数字解析为 Int，as_int64()==7");

        ID::Bench::Check(Child(root, "ratio").is_double() && Child(root, "ratio").as_double() == 0.5, "mixed.ratio",
                         "含小数点的数字解析为 Double，as_double()==0.5");

        ID::Bench::Check(Child(root, "ok").is_bool() && Child(root, "ok").as_bool(), "mixed.ok",
                         "true 解析为 Bool(true)");

        ID::Bench::Check(Child(root, "none").is_null(), "mixed.none", "null 解析为 Null 类型");

        ID::Bench::Check(Child(root, "empty").is_string() && Child(root, "empty").as_string().empty()
                             && Child(root, "empty").type() == JsonType::InlineString,
                         "mixed.empty_string", "空串是长度 0 的内联短串（kInlineStringCapacity=7 内）");

        ID::Bench::Check(Child(root, "s7").type() == JsonType::InlineString
                             && Child(root, "s7").as_string() == "1234567",
                         "mixed.inline_7bytes", "7 字节串走内联存储（InlineString）");

        ID::Bench::Check(Child(root, "s8").type() == JsonType::String
                             && Child(root, "s8").as_string() == "12345678",
                         "mixed.long_8bytes", "8 字节串走长串存储（String），内容完整");

        const JsonValue& arr = Child(root, "arr");
        ID::Bench::Check(arr.is_array() && arr.size() == 4, "mixed.array_size", "数组元素数为 4");
        ID::Bench::Check(arr[0].as_int64() == 1 && arr[1].as_int64() == -2, "mixed.array_ints",
                         "数组内整数元素（含负数）解析正确");
        ID::Bench::Check(arr[2].is_double() && arr[2].as_double() == 3.5, "mixed.array_double",
                         "数组内浮点元素解析正确");
        ID::Bench::Check(arr[3].as_string() == "x", "mixed.array_string", "数组内 1 字节短串解析正确");
        ID::Bench::Check(&arr[9] == &ID::Json::kNullValue, "mixed.array_out_of_range",
                         "越界下标返回 kNullValue（不越界访问，地址可比）");

        const JsonValue& deep = Child(Child(Child(root, "obj"), "nested"), "deep");
        ID::Bench::Check(deep.is_array() && deep.size() == 1 && deep[0].is_array() && deep[0].size() == 0,
                         "mixed.nested_empty_array", "三层嵌套里的空数组 size()==0");

        ID::Bench::Check(root.find("missing") == nullptr, "mixed.find_missing", "未命中返回 nullptr");
        ID::Bench::Check(arr.find("id") == nullptr, "mixed.find_on_non_object", "非对象 find 返回 nullptr");
        ID::Bench::Check(arr.begin() != nullptr && arr.end() != nullptr
                             && static_cast<std::size_t>(arr.end() - arr.begin()) == 4,
                         "mixed.array_iteration", "数组遍历区间长度等于 size()");
        ID::Bench::Check(root.begin() == nullptr && root.end() == nullptr, "mixed.iteration_on_non_array",
                         "非数组 begin()/end() 均为 nullptr（不是合法区间）");

        // 空容器文档：对象/数组的 size() 为 0，且空容器不产生描述块以外的存储
        JsonDocument containers;
        ID::Bench::Check(parse(containers, empty_containers) == JsonError::none, "containers.parse_ok",
                         "空容器文档解析成功");
        const JsonValue& container_root = containers.root();
        ID::Bench::Check(Child(container_root, "a").is_object() && Child(container_root, "a").size() == 0,
                         "containers.empty_object", "{} 是 size()==0 的对象");
        ID::Bench::Check(Child(container_root, "b").is_array() && Child(container_root, "b").size() == 0,
                         "containers.empty_array", "[] 是 size()==0 的数组");
        ID::Bench::Check(Child(container_root, "c")[0].is_object() && Child(container_root, "c")[0].size() == 0,
                         "containers.array_of_empty_object", "[{}] 的元素是空对象");

        // 类型的默认值语义（契约：类型不匹配时返回缺省值，不产生 UB）
        ID::Bench::Check(Child(root, "id").as_double() == 0.0 && Child(root, "id").as_bool() == false
                             && Child(root, "id").as_string().empty(),
                         "mixed.type_mismatch_defaults",
                         "非 Double/Bool/String 上调用 as_* 返回缺省值（README §2.2 访问器语义）");
    }

    void CaseEscapeTable() noexcept
    {
        const std::string expected_decoded = TextFromBytes(kEscapeDecoded, sizeof(kEscapeDecoded));
        const std::string expected_serialized = TextFromBytes(kEscapeSerialized, sizeof(kEscapeSerialized));

        JsonDocument owning;
        ID::Bench::Check(parse(owning, kEscapeText) == JsonError::none, "escape.parse_owning",
                         "转义全表（含 \\u0000/代理对）解析成功");
        const JsonValue& root = owning.root();
        ID::Bench::Check(root.is_string(), "escape.is_string", "根值是字符串");
        ID::Bench::Check(root.as_string().size() == sizeof(kEscapeDecoded), "escape.decoded_length",
                         "解码后字节数为 20（含内嵌 NUL 与 4 字节 UTF-8）");
        ID::Bench::Check(root.as_string() == expected_decoded, "escape.decoded_bytes",
                         "解码字节逐一匹配：\\u0000 保持 0x00，\\u00e9/\\u4e2d/代理对编码为对应 UTF-8");

        JsonDocument non_owning;
        ID::Bench::Check(parse_non_owning(non_owning, kEscapeText) == JsonError::none, "escape.parse_non_owning",
                         "non_owning 解析同一文本成功");
        ID::Bench::Check(non_owning.root().as_string() == expected_decoded, "escape.non_owning_decoded_bytes",
                         "两种模式的解码结果一致");
        const std::string_view escaped_view = non_owning.root().as_string();
        const std::string      escape_input(kEscapeText);
        ID::Bench::Check(!IsInsideInput(escape_input, escaped_view.data(), escaped_view.size()),
                         "escape.non_owning_copies_escaped",
                         "含转义的字符串在 non_owning 模式也复制到 arena（无法零拷贝，README §2.4 注解）");

        // 序列化对称
        char              buffer[128];
        JsonWriteOptions  options{};
        const std::size_t written = serialize(root, buffer, sizeof(buffer), options);

        const std::string actual_hex = HexOf(buffer, written < sizeof(buffer) ? written : sizeof(buffer));
        char              length_detail[512] = {};
        std::snprintf(length_detail, sizeof(length_detail), "期望 %zu 字节，实际 written=%zu；期望字节=%s；实际字节=%s",
                      sizeof(kEscapeSerialized), written, HexOf(expected_serialized.data(),
                                                                expected_serialized.size()).c_str(),
                      actual_hex.c_str());
        ID::Bench::Check(written == sizeof(kEscapeSerialized), "escape.serialize_length", length_detail);
        ID::Bench::Check(std::string(buffer, written) == expected_serialized, "escape.serialize_bytes",
                         length_detail);

        JsonDocument round_trip;
        ID::Bench::Check(parse(round_trip, std::string(buffer, written)) == JsonError::none,
                         "escape.round_trip_parse", "序列化结果可以重新解析");
        ID::Bench::Check(Fingerprint(round_trip.root()) == Fingerprint(root), "escape.round_trip_equivalent",
                         "序列化 → 重新解析不改变内容（指纹逐字节相同）");
    }

    void CaseStringBoundaries() noexcept
    {
        const std::string values[] = {"", "1", "123456", "1234567", "12345678", "123456789012345", "1234567890123456"};

        for (const std::string& value : values)
        {
            const std::string text = "\"" + value + "\"";
            JsonDocument      owning;
            JsonDocument      non_owning;
            const JsonError   owning_code = parse(owning, text);
            const JsonError   non_owning_code = parse_non_owning(non_owning, text);

            char detail[160] = {};
            std::snprintf(detail, sizeof(detail), "长度 %zu 的字符串：期望 InlineString（<=7）或 String（>7）",
                          value.size());
            const JsonType expected_type =
                value.size() <= ID::Json::kInlineStringCapacity ? JsonType::InlineString : JsonType::String;

            ID::Bench::Check(owning_code == JsonError::none && non_owning_code == JsonError::none,
                             std::string("string_boundary.parse.") + std::to_string(value.size()), detail);
            ID::Bench::Check(owning.root().type() == expected_type, 
                             std::string("string_boundary.type.") + std::to_string(value.size()), detail);
            ID::Bench::Check(owning.root().as_string() == value && non_owning.root().as_string() == value,
                             std::string("string_boundary.content.") + std::to_string(value.size()),
                             "两种模式的字符串内容与输入一致");
        }

        // 非零拷贝/零拷贝的边界：≤7 内联在值里，>7 在 non_owning 下直接引用输入
        const std::string inline_text = "\"1234567\"";
        JsonDocument      inline_document;
        ID::Bench::Check(parse_non_owning(inline_document, inline_text) == JsonError::none,
                         "string_boundary.inline_parse", "7 字节串 non_owning 解析成功");
        const std::string_view inline_view = inline_document.root().as_string();
        ID::Bench::Check(!IsInsideInput(inline_text, inline_view.data(), inline_view.size()),
                         "string_boundary.inline_not_zero_copy",
                         "≤7 字节串内联在 JsonValue 里（复制），字符指针不落在输入区间");

        const std::string long_text = "\"12345678\"";
        JsonDocument      long_document;
        ID::Bench::Check(parse_non_owning(long_document, long_text) == JsonError::none,
                         "string_boundary.long_parse", "8 字节串 non_owning 解析成功");
        const std::string_view long_view = long_document.root().as_string();
        ID::Bench::Check(IsInsideInput(long_text, long_view.data(), long_view.size()),
                         "string_boundary.long_zero_copy",
                         ">7 字节无转义串在 non_owning 模式下直接引用输入缓冲（零拷贝）");
        ID::Bench::Check(long_view.data() == long_text.data() + 1, "string_boundary.long_zero_copy_exact",
                         "零拷贝的更强形式：字符指针正好是输入里的那一段（复制必然改变地址）");

        // 长键：无转义键在 non_owning 下引用输入；含转义键在两种模式下都物化到 arena
        const std::string plain_key_text = "{\"key-12345678\":1}";
        JsonDocument      plain_key_document;
        ID::Bench::Check(parse_non_owning(plain_key_document, plain_key_text) == JsonError::none,
                         "string_boundary.plain_key_parse", "长键文档 non_owning 解析成功");
        const ID::Json::Detail::ObjectEntry* plain_entry = plain_key_document.root().members_begin();
        ID::Bench::Check(plain_entry != nullptr && plain_entry->key_length == 12
                             && IsInsideInput(plain_key_text, plain_entry->key, plain_entry->key_length),
                         "string_boundary.plain_key_zero_copy",
                         "无转义长键（12 字节 \"key-12345678\"）在 non_owning 模式下引用输入缓冲");

        const std::string escaped_key_text = "{\"\\u006b\\u0065\\u0079-12345678\":1}";
        JsonDocument      escaped_key_document;
        ID::Bench::Check(parse_non_owning(escaped_key_document, escaped_key_text) == JsonError::none,
                         "string_boundary.escaped_key_parse", "含转义长键文档 non_owning 解析成功");
        const ID::Json::Detail::ObjectEntry* escaped_entry = escaped_key_document.root().members_begin();
        const std::string escaped_key_expected = "key-12345678";
        ID::Bench::Check(escaped_entry != nullptr && escaped_entry->key_length == escaped_key_expected.size()
                             && std::memcmp(escaped_entry->key, escaped_key_expected.data(),
                                            escaped_key_expected.size()) == 0,
                         "string_boundary.escaped_key_content", "含转义长键解码后内容正确（\\u006b→k 等）");
        ID::Bench::Check(escaped_entry != nullptr
                             && !IsInsideInput(escaped_key_text, escaped_entry->key, escaped_entry->key_length),
                         "string_boundary.escaped_key_materialized",
                         "含转义的长键必须物化（不能引用输入里的转义形式）");
    }

    void CaseNumberBoundaries() noexcept
    {
        struct NumberCase
        {
            const char* name;
            const char* text;
            bool        expect_int;
            std::int64_t int_value;      // expect_int 时有效
            double      double_value;    // !expect_int 时按位比较
        };

        const NumberCase cases[] = {
            {"number.int64_max", "9223372036854775807", true, std::numeric_limits<std::int64_t>::max(), 0.0},
            {"number.int64_min", "-9223372036854775808", true, std::numeric_limits<std::int64_t>::min(), 0.0},
            {"number.zero", "0", true, 0, 0.0},
            {"number.negative_zero_integer", "-0", true, 0, 0.0},
            {"number.int64_plus_one_as_double", "9223372036854775808", false, 0, 9223372036854775808.0},
            {"number.int64_min_minus_one_as_double", "-9223372036854775809", false, 0, -9223372036854775808.0},
            {"number.negative_zero_double", "-0.0", false, 0, -0.0},
            {"number.exponent_integral", "1e3", false, 0, 1000.0},
            {"number.exponent_upper", "1E+3", false, 0, 1000.0},
            {"number.exponent_fraction", "2.5e-1", false, 0, 0.25},
            {"number.double_max", "1.7976931348623157e308", false, 0, std::numeric_limits<double>::max()},
            {"number.double_min_normal", "2.2250738585072014e-308", false, 0, std::numeric_limits<double>::min()},
            {"number.double_denormal_min", "5e-324", false, 0, std::numeric_limits<double>::denorm_min()},
            {"number.fraction", "0.75", false, 0, 0.75},
        };

        for (const NumberCase& item : cases)
        {
            JsonDocument      document;
            const JsonError   code = parse(document, std::string(item.text));
            ID::Bench::Check(code == JsonError::none, std::string(item.name) + ".parse_ok",
                             "合法数字字面量必须解析成功");

            const JsonValue& value = document.root();
            if (item.expect_int)
            {
                ID::Bench::Check(value.is_int() && value.as_int64() == item.int_value,
                                 std::string(item.name) + ".int_value",
                                 "整数形态（无小数/指数）解析为 Int 且值精确");
            }
            else
            {
                ID::Bench::Check(value.is_double() && DoubleBits(value.as_double()) == DoubleBits(item.double_value),
                                 std::string(item.name) + ".double_value",
                                 "含小数或指数（或 int64 溢出退化）解析为 Double 且位模式精确");
            }
        }

        // 类型保真：整数形态的 double 序列化后必须带 ".0"，重新解析仍是 Double
        JsonDocument exponent_document;
        ID::Bench::Check(parse(exponent_document, std::string("1e3")) == JsonError::none,
                         "number.exponent_parse", "1e3 解析成功");
        char              buffer[64] = {};
        const std::size_t written = serialize(exponent_document.root(), buffer, sizeof(buffer), JsonWriteOptions{});
        ID::Bench::Check(std::string(buffer, written) == "1000.0", "number.exponent_serialize_type_fidelity",
                         "整数形态的 double 补 \".0\"（否则 round-trip 会退化为 Int，README §2.5）");

        JsonDocument reparsed;
        ID::Bench::Check(parse(reparsed, std::string(buffer, written)) == JsonError::none,
                         "number.exponent_reparse_ok", "补 \".0\" 的结果仍是合法 JSON");
        ID::Bench::Check(reparsed.root().is_double(), "number.exponent_reparse_double",
                         "重新解析后仍是 Double 而不是 Int");
    }

    void CaseNumberErrors() noexcept
    {
        CheckParseErrorOffset("error.invalid_number.leading_zero", "01", JsonError::invalid_number, 1);
        CheckParseErrorOffset("error.invalid_number.double_zero", "00", JsonError::invalid_number, 1);
        CheckParseErrorOffset("error.invalid_number.missing_fraction", "1.", JsonError::invalid_number, 2);
        CheckParseErrorOffset("error.invalid_number.missing_exponent", "1e", JsonError::invalid_number, 2);
        CheckParseErrorOffset("error.invalid_number.missing_exponent_digits", "1e+", JsonError::invalid_number, 3);
        CheckParseErrorOffset("error.invalid_number.lone_minus", "-", JsonError::invalid_number, 1);
        CheckParseErrorOffset("error.invalid_number.minus_then_letter", "-abc", JsonError::invalid_number, 1);
        CheckParseErrorOffset("error.invalid_number.double_overflow", "1e400", JsonError::invalid_number, 0);
        CheckParseErrorOffset("error.invalid_number.negative_overflow", "-1e400", JsonError::invalid_number, 0);
    }

    void CaseDepthBoundary() noexcept
    {
        // 契约（IDJsonParser.hpp）：根值为第 0 层，最深的容器允许到 kMaxJsonDepth - 1 = 255 层，
        // 即最多 256 层容器；第 257 层触发 depth_exceeded。
        const std::string allowed = ID::Bench::JsonFixture::MakeNestedArrayText(256);
        JsonDocument      allowed_document;
        ID::Bench::Check(parse(allowed_document, allowed) == JsonError::none, "depth.256_allowed",
                         "256 层容器（最深为第 255 层）是契约允许的最大嵌套");

        const std::string allowed_objects = ID::Bench::JsonFixture::MakeNestedObjectText(256);
        JsonDocument      allowed_object_document;
        ID::Bench::Check(parse(allowed_object_document, allowed_objects) == JsonError::none,
                         "depth.256_objects_allowed", "256 层嵌套对象同样允许");

        const std::string exceeded = ID::Bench::JsonFixture::MakeNestedArrayText(257);
        CheckParseErrorPosition("error.depth_exceeded.array_257", exceeded, JsonError::depth_exceeded, 256, 1, 257);

        const std::string exceeded_objects = ID::Bench::JsonFixture::MakeNestedObjectText(257);
        CheckParseErrorPosition("error.depth_exceeded.object_257", exceeded_objects, JsonError::depth_exceeded,
                                1280, 1, 1281);

        // 解析出来的最深值必须仍能序列化（写侧上限同为 256，合法值不应触达）
        char              buffer[1024];
        const std::size_t written = serialize(allowed_document.root(), buffer, sizeof(buffer), JsonWriteOptions{});
        ID::Bench::Check(written > 0 && written == 512, "depth.serialize_256_layers",
                         "256 层容器的紧凑序列化长度为 512 字节（256 个 '[' + 256 个 ']'）");
    }

    // ==================== C1/C3：错误路径与位置 ====================

    void CaseUnexpectedToken() noexcept
    {
        CheckParseErrorOffset("error.unexpected_token.object_trailing_comma", "{\"a\":1,}",
                              JsonError::unexpected_token, 7);
        CheckParseErrorOffset("error.unexpected_token.array_trailing_comma", "[1,]", JsonError::unexpected_token, 3);
        CheckParseErrorOffset("error.unexpected_token.array_missing_comma", "[1 2]", JsonError::unexpected_token, 3);
        CheckParseErrorOffset("error.unexpected_token.bad_literal", "tru", JsonError::unexpected_token, 0);
        CheckParseErrorOffset("error.unexpected_token.empty_input", "", JsonError::unexpected_token, 0);
        CheckParseErrorOffset("error.unexpected_token.missing_colon", "{\"a\" 1}", JsonError::unexpected_token, 5);
        CheckParseErrorOffset("error.unexpected_token.unclosed_array", "[", JsonError::unexpected_token, 1);
        CheckParseErrorOffset("error.unexpected_token.unclosed_object", "{", JsonError::unexpected_token, 1);
        CheckParseErrorOffset("error.unexpected_token.unterminated_string", "\"abc", JsonError::unexpected_token, 4);
        CheckParseErrorOffset("error.unexpected_token.unterminated_escape", "\"abc\\", JsonError::unexpected_token, 5);
        CheckParseErrorOffset("error.unexpected_token.missing_key_quotes", "{a:1}", JsonError::unexpected_token, 1);
        CheckParseErrorOffset("error.unexpected_token.single_quote", "'a'", JsonError::unexpected_token, 0);
        CheckParseErrorOffset("error.unexpected_token.nan_literal", "NaN", JsonError::unexpected_token, 0);
        CheckParseErrorOffset("error.unexpected_token.infinity_literal", "Infinity", JsonError::unexpected_token, 0);
    }

    void CaseStringErrors() noexcept
    {
        CheckParseErrorOffset("error.invalid_string_escape.unknown_escape", "\"a\\q\"",
                              JsonError::invalid_string_escape, 3);
        CheckParseErrorOffset("error.invalid_string_escape.short_hex", "\"\\u12\"", JsonError::invalid_string_escape, 5);
        CheckParseErrorOffset("error.invalid_string_escape.non_hex", "\"\\uZZZZ\"", JsonError::invalid_string_escape, 3);

        CheckParseErrorOffset("error.invalid_utf16_surrogate.high_at_end", "\"\\uD800\"",
                              JsonError::invalid_utf16_surrogate, 7);
        CheckParseErrorOffset("error.invalid_utf16_surrogate.lone_low", "\"\\uDC00\"",
                              JsonError::invalid_utf16_surrogate, 3);
        CheckParseErrorOffset("error.invalid_utf16_surrogate.high_then_non_low", "\"\\uD800\\u0041\"",
                              JsonError::invalid_utf16_surrogate, 7);

        // 注意：`\x01b` 在 C++ 里会被贪婪解析成 0x1B，因此必须把字面量拆开写
        CheckParseErrorOffset("error.control_char_in_string.raw_0x01", std::string("\"a\x01") + "b\"",
                              JsonError::control_char_in_string, 2);
        CheckParseErrorOffset("error.control_char_in_string.raw_newline", "\"a\nb\"",
                              JsonError::control_char_in_string, 2);
        CheckParseErrorOffset("error.control_char_in_string.raw_tab", "\"a\tb\"",
                              JsonError::control_char_in_string, 2);
    }

    void CaseTrailingContent() noexcept
    {
        CheckParseErrorOffset("error.trailing_content.second_number", "1 1", JsonError::trailing_content, 2);
        CheckParseErrorOffset("error.trailing_content.word_after_null", "null x", JsonError::trailing_content, 5);
        CheckParseErrorOffset("error.trailing_content.second_object", "{}{}", JsonError::trailing_content, 2);
        CheckParseErrorOffset("error.trailing_content.second_array", "[] []", JsonError::trailing_content, 3);
        CheckParseErrorOffset("error.trailing_content.hex_literal", "0x10", JsonError::trailing_content, 1);
    }

    void CaseErrorPositions() noexcept
    {
        // 多行：`"b": ,` 的逗号就是出错位置。字节布局（0 起）：
        // 0'{' 1'\n' 2' ' 3' ' 4'"' 5'a' 6'"' 7':' 8' ' 9'1' 10',' 11'\n' 12' ' 13' ' 14'"' 15'b'
        // 16'"' 17':' 18' ' 19',' 20'\n' 21'}'  → offset 19，line 3（前有两个 '\n'），column 8（19-12+1）
        const std::string multi_line = "{\n  \"a\": 1,\n  \"b\": ,\n}";
        CheckParseErrorPosition("error.position.multi_line", multi_line, JsonError::unexpected_token, 19, 3, 8);

        // CRLF：行号只由 '\n' 递增，'\r' 计入列宽。`[1,\r\n 2 x]`：
        // 0'[' 1'1' 2',' 3'\r' 4'\n' 5' ' 6'2' 7' ' 8'x' 9']' → offset 8，line 2，column 8-5+1=4
        const std::string crlf = "[1,\r\n 2 x]";
        CheckParseErrorPosition("error.position.crlf", crlf, JsonError::unexpected_token, 8, 2, 4);

        // 行尾截断：offset 等于输入长度（"在末尾期待更多内容"），line/column 指向尾后位置
        const std::string truncated_one_line = "{\"a\":1";
        CheckParseErrorPosition("error.position.truncated_single_line", truncated_one_line,
                                JsonError::unexpected_token, 6, 1, 7);

        const std::string truncated_multi_line = "{\n \"a\": 1";
        CheckParseErrorPosition("error.position.truncated_multi_line", truncated_multi_line,
                                JsonError::unexpected_token, 9, 2, 8);

        // 字符串本身从第 2 行开始（换行在字符串之外），控制字符是该行第 2 个字节：
        // `[`0 '\n'1 '"'2 0x01@3 → offset 3、line 2、column 3-2+1=2
        // （裸换行若出现在字符串内部，它自己就是 control_char_in_string，无法构造"第 2 行第 1 列"的字符串控制字符）
        CheckParseErrorPosition("error.position.control_char_second_line", std::string("[\n\"\x01") + "\"]",
                                JsonError::control_char_in_string, 3, 2, 2);
    }

    void CaseErrorCodeTexts() noexcept
    {
        struct ErrorName
        {
            JsonError   code;
            const char* text;
        };
        const ErrorName names[] = {
            {JsonError::none, "none"},
            {JsonError::out_of_memory, "out_of_memory"},
            {JsonError::unexpected_token, "unexpected_token"},
            {JsonError::invalid_number, "invalid_number"},
            {JsonError::invalid_string_escape, "invalid_string_escape"},
            {JsonError::invalid_utf16_surrogate, "invalid_utf16_surrogate"},
            {JsonError::control_char_in_string, "control_char_in_string"},
            {JsonError::depth_exceeded, "depth_exceeded"},
            {JsonError::trailing_content, "trailing_content"},
            {JsonError::input_too_large, "input_too_large"},
            {JsonError::not_initialized, "not_initialized"},
        };

        for (const ErrorName& item : names)
        {
            ID::Bench::Check(std::string_view(ID::Json::to_string(item.code)) == item.text,
                             std::string("to_string.error.") + item.text,
                             "JsonError 的稳定文本（README §2.4：不需要释放、可直接跨线程使用）");
        }
        ID::Bench::Check(std::string_view(ID::Json::to_string(static_cast<JsonError>(200))) == "unknown_error",
                         "to_string.error.unknown_value", "未知错误码返回 \"unknown_error\"，不返回 nullptr");

        ID::Bench::Check(std::string_view(ID::Json::to_string(JsonWriteError::none)) == "none",
                         "to_string.write.none", "JsonWriteError::none 的稳定文本");
        ID::Bench::Check(std::string_view(ID::Json::to_string(JsonWriteError::non_finite_number)) == "non_finite_number",
                         "to_string.write.non_finite_number", "JsonWriteError::non_finite_number 的稳定文本");
        ID::Bench::Check(std::string_view(ID::Json::to_string(JsonWriteError::too_deep)) == "too_deep",
                         "to_string.write.too_deep", "JsonWriteError::too_deep 的稳定文本");
        ID::Bench::Check(std::string_view(ID::Json::to_string(static_cast<JsonWriteError>(200))) == "unknown_error",
                         "to_string.write.unknown_value", "未知写错误码返回 \"unknown_error\"");
    }

    // ==================== C3/C4：失败语义与文档状态 ====================

    void CaseFailureSemantics() noexcept
    {
        // 失败后文档被置空，且把 null 根值序列化成 "null"
        JsonDocument   document;
        JsonParseError error{};
        const JsonError code = parse(document, "{\"a\":", &error);
        char            code_detail[128] = {};
        std::snprintf(code_detail, sizeof(code_detail), "code=%s offset=%u（不完整对象应报 unexpected_token）",
                      ID::Json::to_string(code), error.offset);
        ID::Bench::Check(code == JsonError::unexpected_token, "failure.parse_fails", code_detail);
        ID::Bench::Check(document.root().is_null(), "failure.root_null",
                         "契约（README §6）：解析失败时文档被置空，root().is_null()");

        char              buffer[32] = {};
        const std::size_t written = serialize(document.root(), buffer, sizeof(buffer), JsonWriteOptions{});
        ID::Bench::Check(written == 4 && std::string_view(buffer, 4) == "null", "failure.serialize_null_root",
                         "失败后的 null 根值序列化为 4 字节 \"null\"（不是 0 长度）");

        // 成功后再失败：旧结果一次性失效（游标回退、根值置空），不复用任何旧指针
        JsonDocument reused;
        ID::Bench::Check(parse(reused, "{\"keep\":\"0123456789\"}") == JsonError::none, "failure.reuse_first_ok",
                         "先成功解析一个文档");
        const std::size_t used_after_success = reused.arena().used_bytes();
        ID::Bench::Check(used_after_success > 0, "failure.used_bytes_positive",
                         "解析后 arena 游标已经推进（used_bytes > 0）");
        ID::Bench::Check(parse(reused, "{}") == JsonError::none, "failure.reuse_second_ok",
                         "parse 进入即 reset（README §5）：第二次解析成功");
        ID::Bench::Check(reused.arena().used_bytes() < used_after_success, "failure.reuse_used_bytes_rolled_back",
                         "第二次解析从游标起点开始（只读统计，不读取任何已失效指针）");

        // 未就绪文档：构造不失败，parse 返回 not_initialized
        JsonDocument too_small(1, ID::Bench::JsonFixture::CountingSegmentAlloc,
                               ID::Bench::JsonFixture::CountingSegmentFree);
        ID::Bench::Check(!too_small.is_ready(), "failure.too_small_not_ready",
                         "block_size 不大于段头时 is_ready() 为 false（构造不抛异常）");
        JsonParseError not_ready_error{};
        ID::Bench::Check(parse(too_small, "1", &not_ready_error) == JsonError::not_initialized,
                         "failure.not_initialized_code", "未就绪文档的 parse 返回 not_initialized");
        ID::Bench::Check(too_small.root().is_null(), "failure.not_initialized_root_null",
                         "未就绪文档的根值保持 null");
        too_small.reset();
        ID::Bench::Check(!too_small.is_ready(), "failure.reset_keeps_not_ready",
                         "reset() 不会把未初始化的 arena 变成就绪（README §2.3 统计口径）");

        JsonDocument null_alloc(ID::Json::kDefaultBlockSize, nullptr, nullptr);
        ID::Bench::Check(!null_alloc.is_ready(), "failure.null_alloc_not_ready",
                         "alloc_fn 为 nullptr 时 is_ready() 为 false（构造参数契约）");
        ID::Bench::Check(parse(null_alloc, "1") == JsonError::not_initialized, "failure.null_alloc_parse",
                         "alloc_fn 为空的文档 parse 返回 not_initialized");
    }

    // ==================== C7：序列化 ====================

    void CaseSerializeCompact() noexcept
    {
        struct CompactCase
        {
            const char* name;
            const char* input;
            const char* expected;
        };
        const CompactCase cases[] = {
            {"serialize.compact.object", "{\"id\":7,\"name\":\"demo\",\"tags\":[\"a\",\"b\"],\"ratio\":0.5}",
             "{\"id\":7,\"name\":\"demo\",\"tags\":[\"a\",\"b\"],\"ratio\":0.5}"},
            {"serialize.compact.null", "null", "null"},
            {"serialize.compact.true", "true", "true"},
            {"serialize.compact.false", "false", "false"},
            {"serialize.compact.int", "-3", "-3"},
            {"serialize.compact.empty_array", "[]", "[]"},
            {"serialize.compact.empty_object", "{}", "{}"},
            {"serialize.compact.string", "\"x\"", "\"x\""},
        };

        for (const CompactCase& item : cases)
        {
            JsonDocument document;
            ID::Bench::Check(parse(document, std::string(item.input)) == JsonError::none,
                             std::string(item.name) + ".parse_ok", "输入可以解析");
            char              buffer[256] = {};
            const std::size_t written = serialize(document.root(), buffer, sizeof(buffer), JsonWriteOptions{});
            ID::Bench::Check(std::string(buffer, written) == item.expected, std::string(item.name) + ".bytes",
                             "紧凑序列化逐字节等于期望输出（成员按插入序、无多余空白）");
        }
    }

    void CaseSerializeIndent() noexcept
    {
        JsonDocument document;
        ID::Bench::Check(parse(document, std::string("{\"a\":1,\"b\":[1,2],\"c\":{}}")) == JsonError::none,
                         "serialize.indent_parse_ok", "缩进用例的输入解析成功");

        JsonWriteOptions options{};
        options.indent = 2;

        // 逐字节期望：每层缩进 2 个空格、每成员一行、对象成员用 ": " 分隔、空容器保持紧凑（"{}"）
        const std::string expected = "{\n  \"a\": 1,\n  \"b\": [\n    1,\n    2\n  ],\n  \"c\": {}\n}";

        char              buffer[256] = {};
        const std::size_t written = serialize(document.root(), buffer, sizeof(buffer), options);
        ID::Bench::Check(std::string(buffer, written) == expected, "serialize.indent.bytes",
                         "indent=2 的逐字节期望输出（依据 README §2.5：>0 表示每层缩进的空格数）");

        // 缩进不改变语义：重新解析后指纹相同
        JsonDocument reparsed;
        ID::Bench::Check(parse(reparsed, std::string(buffer, written)) == JsonError::none,
                         "serialize.indent.reparse_ok", "缩进输出是合法 JSON");
        ID::Bench::Check(Fingerprint(reparsed.root()) == Fingerprint(document.root()),
                         "serialize.indent.semantics_unchanged", "缩进不改变语义（指纹相同）");

        // indent = 1 与 4 也各给一份小样，证明缩进宽度按参数生效
        options.indent = 4;
        const std::size_t written4 = serialize(document.root(), buffer, sizeof(buffer), options);
        const std::string expected4 = "{\n    \"a\": 1,\n    \"b\": [\n        1,\n        2\n    ],\n    \"c\": {}\n}";
        ID::Bench::Check(std::string(buffer, written4) == expected4, "serialize.indent4.bytes",
                         "indent=4 时每层 4 个空格（缩进宽度由参数决定）");
    }

    void CaseSerializeBufferContract() noexcept
    {
        const std::string text = "{\"a\":[1,2,3],\"b\":\"value\",\"c\":true}";
        JsonDocument      document;
        ID::Bench::Check(parse(document, text) == JsonError::none, "serialize.buffer.parse_ok", "输入解析成功");

        JsonWriteOptions options{};
        const std::size_t required = serialize(document.root(), nullptr, 0, options);
        ID::Bench::Check(required == text.size(), "serialize.buffer.probe_null_out",
                         "out == nullptr 时只探测所需字节数（README §2.5）");

        constexpr char kSentinel = static_cast<char>(0x7F);
        std::vector<char> buffer(required + 8, kSentinel);

        const std::size_t short_written = serialize(document.root(), buffer.data(), required - 1, options);
        ID::Bench::Check(short_written == required, "serialize.buffer.capacity_short_returns_required",
                         "容量不足不算失败：返回所需字节数（out 内容未定义，不检查内容）");

        const std::size_t exact_written = serialize(document.root(), buffer.data(), required, options);
        ID::Bench::Check(exact_written == required && buffer[required] == kSentinel,
                         "serialize.buffer.capacity_exact_no_nul",
                         "capacity 恰好等于返回值时不写结尾 NUL（README §2.5）");

        const std::size_t extra_written = serialize(document.root(), buffer.data(), required + 1, options);
        ID::Bench::Check(extra_written == required && buffer[required] == '\0',
                         "serialize.buffer.capacity_extra_writes_nul",
                         "capacity >= 返回值 + 1 时在 out[返回值] 写 NUL");
        ID::Bench::Check(std::memcmp(buffer.data(), text.data(), required) == 0, "serialize.buffer.bytes_identical",
                         "写出的内容与输入逐字节相同（成员按插入序）");
    }

    void CaseSerializeDoubleRoundTrip() noexcept
    {
        struct DoubleCase
        {
            const char* name;
            const char* json_text;
            double      value;
        };
        const DoubleCase cases[] = {
            {"roundtrip.zero", "0.0", 0.0},
            {"roundtrip.negative_zero", "-0.0", -0.0},
            {"roundtrip.one", "1.0", 1.0},
            {"roundtrip.tenth", "0.1", 0.1},
            {"roundtrip.third", "0.3333333333333333", 0.3333333333333333},
            {"roundtrip.max", "1.7976931348623157e308", std::numeric_limits<double>::max()},
            {"roundtrip.min_normal", "2.2250738585072014e-308", std::numeric_limits<double>::min()},
            {"roundtrip.denormal_min", "5e-324", std::numeric_limits<double>::denorm_min()},
            {"roundtrip.small_negative", "-1.5e-3", -0.0015},
        };

        for (const DoubleCase& item : cases)
        {
            JsonDocument document;
            ID::Bench::Check(parse(document, std::string(item.json_text)) == JsonError::none,
                             std::string(item.name) + ".parse_ok", "定点浮点字面量解析成功");
            ID::Bench::Check(DoubleBits(document.root().as_double()) == DoubleBits(item.value),
                             std::string(item.name) + ".parse_bits", "解析结果的位模式与期望一致");

            char              buffer[64] = {};
            const std::size_t written = serialize(document.root(), buffer, sizeof(buffer), JsonWriteOptions{});
            const std::string serialized(buffer, written);
            ID::Bench::Check(written > 0, std::string(item.name) + ".serialize_ok", "序列化成功（返回非 0）");

            bool has_type_marker = false;
            for (const char symbol : serialized)
            {
                if (symbol == '.' || symbol == 'e' || symbol == 'E')
                {
                    has_type_marker = true;
                    break;
                }
            }
            ID::Bench::Check(has_type_marker, std::string(item.name) + ".type_marker",
                             "输出必含 '.' 或指数，保证读回仍是 Double（README §2.5 类型保真）");

            JsonDocument reparsed;
            ID::Bench::Check(parse(reparsed, serialized) == JsonError::none,
                             std::string(item.name) + ".reparse_ok", "序列化结果可以重新解析");
            ID::Bench::Check(reparsed.root().is_double()
                                 && DoubleBits(reparsed.root().as_double()) == DoubleBits(item.value),
                             std::string(item.name) + ".roundtrip_bits",
                             "序列化 → 重新解析保持位模式（最短往返表示）");
        }
    }

    void CaseSerializeNonFinite() noexcept
    {
        // non_finite_number 只能通过手工构造的 Double 值触达：解析器拒绝 NaN/Infinity 字面量
        // （README §8.7），而写侧公开头明确"手工构造的值也在这里被拦住"（IDJsonWriter.hpp）。
        // JsonValue 的 16 字节布局与 m_payload/m_type 由公开头声明（README §2.2），这里只写不改。
        struct NonFiniteCase
        {
            const char* name;
            double      value;
        };
        const NonFiniteCase cases[] = {
            {"serialize.non_finite.nan", std::numeric_limits<double>::quiet_NaN()},
            {"serialize.non_finite.infinity", std::numeric_limits<double>::infinity()},
            {"serialize.non_finite.negative_infinity", -std::numeric_limits<double>::infinity()},
        };

        for (const NonFiniteCase& item : cases)
        {
            JsonValue value{};
            value.m_type = JsonType::Double;
            value.m_payload.d = item.value;

            constexpr char    kSentinel = static_cast<char>(0x7F);
            char              buffer[16];
            std::memset(buffer, kSentinel, sizeof(buffer));

            JsonWriteError    error = JsonWriteError::none;
            const std::size_t written = serialize(value, buffer, sizeof(buffer), JsonWriteOptions{}, &error);

            ID::Bench::Check(written == 0, std::string(item.name) + ".returns_zero",
                             "契约：序列化失败返回 0（不写 null、不写 0）");
            ID::Bench::Check(error == JsonWriteError::non_finite_number, std::string(item.name) + ".error_code",
                             "契约：非有限值报 non_finite_number");
            ID::Bench::Check(buffer[0] == kSentinel, std::string(item.name) + ".no_output",
                             "单个非有限值不产生任何输出字节（不做静默替换）");
        }

        // 有限值的对照：证明上一组断言不是"恒不写"
        JsonValue finite{};
        finite.m_type = JsonType::Double;
        finite.m_payload.d = 2.5;
        char              buffer[16] = {};
        JsonWriteError    error = JsonWriteError::none;
        const std::size_t written = serialize(finite, buffer, sizeof(buffer), JsonWriteOptions{}, &error);
        ID::Bench::Check(written == 3 && std::string_view(buffer, 3) == "2.5" && error == JsonWriteError::none,
                         "serialize.non_finite.finite_control", "对照：同一个出口对有限值正常写出 2.5");
    }

    // ==================== C5：并发契约 ====================

    void CaseConcurrency() noexcept
    {
        // 契约（README §5）：serialize 是纯函数，可并发读同一个 root；
        // 不同线程各自持有各文档并行解析是安全的（同一文档跨线程共享属于违约，不在此触发）。
        const ID::Bench::JsonFixture::SampleDocument sample = ID::Bench::JsonFixture::MakeConfigDocument();
        JsonDocument document;
        ID::Bench::Check(parse(document, sample.text) == JsonError::none, "concurrency.parse_ok",
                         "并发用例的输入解析成功");

        char              reference[65536] = {};
        const std::size_t reference_length = serialize(document.root(), reference, sizeof(reference),
                                                       JsonWriteOptions{});
        ID::Bench::Check(reference_length > 0 && reference_length < sizeof(reference),
                         "concurrency.reference_serialize", "单线程参考输出长度在缓冲容量内");

        const std::string reference_fingerprint = Fingerprint(document.root());

        constexpr int kThreads = 4;
        constexpr int kIterations = 64;

        bool                     serial_ok[kThreads] = {};
        bool                     parse_ok[kThreads] = {};
        std::vector<std::thread> workers;
        workers.reserve(kThreads);
        for (int index = 0; index < kThreads; ++index)
        {
            // 每个线程只读共享 root（serialize 契约）与各自的文档（parse 契约），不跨线程共享可变状态
            workers.emplace_back([&document, &reference, reference_length, &reference_fingerprint, &serial_ok,
                                  &parse_ok, &sample, index]() {
                char buffer[65536] = {};
                bool same = true;
                for (int round = 0; round < kIterations; ++round)
                {
                    const std::size_t written = serialize(document.root(), buffer, sizeof(buffer),
                                                          JsonWriteOptions{});
                    if (written != reference_length || std::memcmp(buffer, reference, reference_length) != 0)
                    {
                        same = false;
                        break;
                    }
                }
                serial_ok[index] = same;

                JsonDocument local;
                bool         local_ok = true;
                for (int round = 0; round < kIterations; ++round)
                {
                    if (parse(local, sample.text) != JsonError::none
                        || Fingerprint(local.root()) != reference_fingerprint)
                    {
                        local_ok = false;
                        break;
                    }
                }
                parse_ok[index] = local_ok;
            });
        }
        for (std::thread& worker : workers)
        {
            worker.join();
        }

        for (int index = 0; index < kThreads; ++index)
        {
            ID::Bench::Check(serial_ok[index], std::string("concurrency.serialize.") + std::to_string(index),
                             "4 线程并发序列化同一个 root，输出与单线程参考逐字节相同（纯函数契约）");
            ID::Bench::Check(parse_ok[index], std::string("concurrency.parse.") + std::to_string(index),
                             "4 线程各自持有 JsonDocument 并行解析，结构与本进程单线程结果相同");
        }
    }

    // ==================== C2/C6：arena 统计口径与段来源 ====================

    void CaseArenaAccounting() noexcept
    {
        ID::Bench::JsonFixture::ResetSegmentCounters();

        {
            JsonDocument document(64 * 1024, ID::Bench::JsonFixture::CountingSegmentAlloc,
                                  ID::Bench::JsonFixture::CountingSegmentFree);
            ID::Bench::Check(document.is_ready(), "arena.ready", "自定义段来源的文档构造函数成功");

            const std::string text = ID::Bench::JsonFixture::MakeBulkDocument().text;
            ID::Bench::Check(parse(document, text) == JsonError::none, "arena.parse_bulk",
                             "大对象数组混合文档解析成功");

            const std::size_t capacity_first = document.arena().capacity();
            const std::size_t blocks_first   = document.arena().block_count();
            const std::size_t used_first     = document.arena().used_bytes();
            char              detail[192] = {};
            std::snprintf(detail, sizeof(detail),
                          "capacity=%zu blocks=%zu used=%zu（SegmentedArena：capacity 为净负载之和，used 含对齐填充）",
                          capacity_first, blocks_first, used_first);
            ID::Bench::Check(capacity_first > 0 && blocks_first >= 1 && used_first > 0, "arena.first_parse_stats",
                             detail);

            const bool consistent = used_first <= capacity_first;
            ID::Bench::Check(consistent, "arena.used_not_above_capacity",
                             "每次分配都计入 used_bytes 且不超过 capacity（O(1) 统计口径）");
            const bool expected_blocks = blocks_first > 1;
            ID::Bench::Check(expected_blocks, "arena.multi_block",
                             "150 KB 级文档在 64 KiB 段下需要多个常规段");

            document.reset();
            ID::Bench::Check(document.arena().used_bytes() == 0, "arena.reset_used_zero",
                             "reset() 把游标累计字节数复位为 0");
            // 契约（SegmentedArena）：reset() 回退常规段游标并**归还巨段**、保留常规段。
            // 因此 capacity/block_count 只允许不增加；常规段部分必须保留（capacity 仍 > 0）。
            const std::size_t capacity_after_reset = document.arena().capacity();
            const std::size_t blocks_after_reset   = document.arena().block_count();
            char reset_detail[192] = {};
            std::snprintf(reset_detail, sizeof(reset_detail),
                          "reset 前 capacity=%zu blocks=%zu；reset 后 capacity=%zu blocks=%zu"
                          "（差额来自被归还的巨段，常规段保留）",
                          capacity_first, blocks_first, capacity_after_reset, blocks_after_reset);
            ID::Bench::Check(capacity_after_reset > 0 && capacity_after_reset <= capacity_first,
                             "arena.reset_capacity_non_increasing", reset_detail);
            ID::Bench::Check(blocks_after_reset >= 1 && blocks_after_reset <= blocks_first,
                             "arena.reset_blocks_non_increasing", reset_detail);
            ID::Bench::Check(document.root().is_null(), "arena.reset_root_null", "reset() 把根值置为 null");

            ID::Bench::Check(parse(document, text) == JsonError::none, "arena.reparse_ok",
                             "reset() 之后重新解析同一文档成功");
            ID::Bench::Check(document.arena().capacity() == capacity_first && document.arena().block_count() == blocks_first,
                             "arena.reparse_same_capacity",
                             "复用同一文档重复解析不新增段（容量与段数不变）");
            ID::Bench::Check(document.arena().used_bytes() == used_first, "arena.reparse_same_used",
                             "重复解析的 used_bytes 与首次一致（统计口径可复现）");
        }

        ID::Bench::Check(ID::Bench::JsonFixture::g_segment_counters.free_calls
                             == ID::Bench::JsonFixture::g_segment_counters.alloc_calls,
                         "arena.alloc_free_symmetric",
                         "析构后归还次数等于申请次数（段来源成对使用，无泄漏）");
        ID::Bench::JsonFixture::ResetSegmentCounters();
    }

    void CaseOversizeBlockReturn() noexcept
    {
        ID::Bench::JsonFixture::ResetSegmentCounters();

        {
            JsonDocument document(64 * 1024, ID::Bench::JsonFixture::CountingSegmentAlloc,
                                  ID::Bench::JsonFixture::CountingSegmentFree);
            ID::Bench::Check(parse(document, std::string("{\"a\":1}")) == JsonError::none, "oversize.parse_ok",
                             "先解析一个小文档建立常规段");
            const std::size_t capacity_before = document.arena().capacity();
            const std::size_t blocks_before   = document.arena().block_count();
            const std::size_t frees_before    = ID::Bench::JsonFixture::g_segment_counters.free_calls;

            // 300 KiB > 巨段阈值（64 KiB 段净容量的 1/4 ≈ 16 KiB）→ 走巨段
            void* big = document.arena().allocate(300 * 1024, alignof(std::max_align_t));
            ID::Bench::Check(big != nullptr, "oversize.allocate_ok", "超过巨段阈值的请求分配成功");

            const std::size_t blocks_after_big   = document.arena().block_count();
            const std::size_t capacity_after_big = document.arena().capacity();
            ID::Bench::Check(blocks_after_big == blocks_before + 1, "oversize.block_count_plus_one",
                             "巨段计入 block_count（与 capacity 口径一致，见 SegmentedArena 注释）");
            ID::Bench::Check(capacity_after_big == capacity_before + 300 * 1024, "oversize.capacity_payload",
                             "巨段只把净负载计入 capacity（段头不计入）");

            document.reset();
            ID::Bench::Check(document.arena().block_count() == blocks_before, "oversize.reset_returns_block",
                             "reset() 归还巨段（block_count 回到解析后的值）");
            ID::Bench::Check(document.arena().capacity() == capacity_before, "oversize.reset_capacity",
                             "reset() 从 capacity 中扣减巨段净负载");
            ID::Bench::Check(document.arena().used_bytes() == 0, "oversize.reset_used_zero",
                             "reset() 后 used_bytes 归零");
            ID::Bench::Check(ID::Bench::JsonFixture::g_segment_counters.free_calls == frees_before + 1,
                             "oversize.reset_frees_source",
                             "巨段在 reset() 时归还给段来源（free 次数 +1，README 与 Arena 契约一致）");
        }

        ID::Bench::Check(ID::Bench::JsonFixture::g_segment_counters.free_calls
                             == ID::Bench::JsonFixture::g_segment_counters.alloc_calls,
                         "oversize.alloc_free_symmetric", "析构后申请与归还次数相等");
        ID::Bench::JsonFixture::ResetSegmentCounters();
    }

    void CaseOutOfMemory() noexcept
    {
        // 段来源只放行 1 次申请：解析需要 2 次以上 → 必须返回 out_of_memory 而不是崩溃或半成品
        ID::Bench::JsonFixture::ResetSegmentCounters(1);
        {
            JsonDocument document(64 * 1024, ID::Bench::JsonFixture::CountingSegmentAlloc,
                                  ID::Bench::JsonFixture::CountingSegmentFree);
            const std::string text = ID::Bench::JsonFixture::MakeLongStringDocument(300 * 1024);
            JsonParseError    error{};
            const JsonError   code = parse(document, text, &error);

            char detail[192] = {};
            std::snprintf(detail, sizeof(detail),
                          "段来源上限 1 次；实际 code=%s alloc_calls=%zu block_count=%zu", ID::Json::to_string(code),
                          ID::Bench::JsonFixture::g_segment_counters.alloc_calls, document.arena().block_count());
            ID::Bench::Check(code == JsonError::out_of_memory, "out_of_memory.code", detail);
            ID::Bench::Check(document.root().is_null(), "out_of_memory.root_null",
                             "分配失败的解析不留下半个对象（文档被置空）");
            ID::Bench::Check(document.arena().used_bytes() == 0, "out_of_memory.used_bytes_zero",
                             "失败路径同样回退 arena 游标（reset() 在失败后再次执行）");
        }
        ID::Bench::Check(ID::Bench::JsonFixture::g_segment_counters.alloc_calls == 1,
                         "out_of_memory.single_allocation",
                         "注入的上限只放行了 1 次成功申请（证明失败确实来自段来源耗尽）");
        ID::Bench::Check(ID::Bench::JsonFixture::g_segment_counters.free_calls
                             == ID::Bench::JsonFixture::g_segment_counters.alloc_calls,
                         "out_of_memory.alloc_free_symmetric", "失败后析构仍然归还了已申请的段");
        ID::Bench::JsonFixture::ResetSegmentCounters();
    }

    void CaseDefaultSegmentSource() noexcept
    {
        void* block = ID::Json::default_segment_alloc(1024, alignof(std::max_align_t));
        ID::Bench::Check(block != nullptr, "segment_source.alloc_default", "默认段来源按 max_align 对齐申请成功");
        ID::Json::default_segment_free(block);

        ID::Bench::Check(ID::Json::default_segment_alloc(1024, alignof(std::max_align_t) * 2) == nullptr,
                         "segment_source.reject_oversized_alignment",
                         "对齐要求超过 max_align 时返回 nullptr（不返回对齐不足的内存，README §2.3）");
        ID::Json::default_segment_free(nullptr);
        ID::Bench::Check(true, "segment_source.free_null_is_noop", "默认归还接受 nullptr（契约：可为 nullptr）");

        // 段大小边界：常规段头是两个指针（x64 为 16 字节、x86 为 8 字节），block_size 必须**大于**段头。
        // 这里按当前目标的指针宽度算段头尺寸，不写死 16：32 位目标上 16 已经大于 8，会配置成功。
        constexpr std::size_t kBlockHeaderBytes = 2 * sizeof(void*);
        JsonDocument too_small(kBlockHeaderBytes, ID::Bench::JsonFixture::CountingSegmentAlloc,
                               ID::Bench::JsonFixture::CountingSegmentFree);
        ID::Bench::Check(!too_small.is_ready(), "segment_source.block_size_at_header_fails",
                         "block_size == 段头尺寸（2 个指针）时 arena 配置失败（契约：必须大于段头）");
        JsonDocument minimal(kBlockHeaderBytes + 16, ID::Bench::JsonFixture::CountingSegmentAlloc,
                             ID::Bench::JsonFixture::CountingSegmentFree);
        ID::Bench::Check(minimal.is_ready(), "segment_source.minimal_block_ready",
                         "block_size 略大于段头时配置成功");
        ID::Bench::Check(parse(minimal, std::string("{\"a\":\"0123456789\"}")) == JsonError::none,
                         "segment_source.minimal_block_parse",
                         "最小可用段大小下解析仍成功（必要时申请多个段）");
        ID::Bench::JsonFixture::ResetSegmentCounters();
    }

    // ==================== C2/C6：大输入跨巨段 ====================

    void CaseLargeInputs() noexcept
    {
        // 512 KiB 单字符串：远超 64 KiB 段的净容量，必须走巨段
        const std::string long_text = ID::Bench::JsonFixture::MakeLongStringDocument(512 * 1024);
        {
            JsonDocument document;
            ID::Bench::Check(parse(document, long_text) == JsonError::none, "large.parse_owning",
                             "512 KiB 单字符串（owning）解析成功");
            const std::string_view view = Child(document.root(), "big").as_string();
            ID::Bench::Check(view.size() == 512 * 1024, "large.owning_length",
                             "长字符串长度完整（未被段边界截断）");
            bool all_x = view.size() == 512 * 1024;
            for (std::size_t index = 0; all_x && index < view.size(); index += 4096)
            {
                all_x = view[index] == 'x';
            }
            ID::Bench::Check(all_x && view.back() == 'x', "large.owning_content",
                             "长字符串内容完整（首尾与每 4 KiB 采样点均为 'x'）");
            ID::Bench::Check(document.arena().block_count() >= 2, "large.oversize_block_count",
                             "跨巨段：512 KiB 请求超过常规段净容量，必然走巨段（block_count ≥ 2 含常规段与巨段）");
            ID::Bench::Check(!IsInsideInput(long_text, view.data(), view.size()), "large.owning_copies",
                             "owning 模式的字符数据在 arena 上（不引用输入缓冲）");
        }

        {
            JsonDocument document;
            ID::Bench::Check(parse_non_owning(document, long_text) == JsonError::none, "large.parse_non_owning",
                             "512 KiB 单字符串（non_owning）解析成功");
            const std::string_view view = Child(document.root(), "big").as_string();
            ID::Bench::Check(view.size() == 512 * 1024 && IsInsideInput(long_text, view.data(), view.size()),
                             "large.non_owning_zero_copy_big",
                             ">7 字节无转义长串在 non_owning 下仍引用输入（跨巨段时同样零拷贝）");
        }

        // 约 230 KB 的整数数组：元素多、逐个入 arena
        const std::string array_text = ID::Bench::JsonFixture::MakeIntegerArrayDocument(50000);
        {
            JsonDocument document;
            ID::Bench::Check(parse(document, array_text) == JsonError::none, "large.array_parse",
                             "5 万元素整数数组解析成功");
            const JsonValue& root = document.root();
            ID::Bench::Check(root.is_array() && root.size() == 50000, "large.array_size",
                             "数组元素数完整（扩容按 2 倍换块，旧块遗弃）");
            ID::Bench::Check(root[0].as_int64() == 0 && root[49999].as_int64() == 49999, "large.array_values",
                             "首尾元素值正确（扩容后的数据搬迁未丢元素）");
        }
    }

    // ==================== C2：input_too_large ====================

    void CaseInputTooLarge() noexcept
    {
#if !defined(_WIN32)
        // 平台非目标：本域未声称 Linux/其它平台支持，也没有 4 GiB+1 地址区间的构造手段
        ID::Bench::Uncovered("parse.input_too_large",
                             "非 Windows 平台没有本用例所需的地址区间构造手段（未声称 Linux 支持）");
#elif !defined(_WIN64)
        // 32 位目标：size_t 装不下 4 GiB + 1 字节，进程地址空间也不足；实现侧对 32 位目标用
        // if constexpr 折掉了该长度判定（IDJsonParser.cpp 的 length 检查），因此本变体无受控取证手段
        ID::Bench::Uncovered("parse.input_too_large",
                             "32 位目标（_WIN64 未定义）：size_t 与进程地址空间都装不下 4 GiB + 1 字节，"
                             "无法构造该输入；实现侧对该目标用 if constexpr 折掉了长度判定，本变体无受控取证手段");
#else
        // 4 GiB + 1 字节：实现按长度先行拒绝，不读取任何字节，因此"只保留地址区间"也是可用的构造方式
        constexpr std::size_t kHugeSize = static_cast<std::size_t>(0x100000000ull) + 1u;

        void* base = ::VirtualAlloc(nullptr, kHugeSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        const char* mode = "MEM_RESERVE|MEM_COMMIT";
        if (base == nullptr)
        {
            base = ::VirtualAlloc(nullptr, kHugeSize, MEM_RESERVE, PAGE_READWRITE);
            mode = "MEM_RESERVE only（未提交，实现不读取任何字节）";
        }

        if (base == nullptr)
        {
            char detail[224] = {};
            std::snprintf(detail, sizeof(detail),
                          "VirtualAlloc 无法取得 4 GiB + 1 字节地址区间（GetLastError=%lu）："
                          "64 位进程内存/提交上限不足，本机无法构造该输入",
                          static_cast<unsigned long>(::GetLastError()));
            ID::Bench::Uncovered("parse.input_too_large", detail);
            return;
        }

        {
            const std::string_view huge_text(static_cast<const char*>(base), kHugeSize);
            JsonDocument           document;
            JsonParseError         error{};
            const JsonError        code = parse(document, huge_text, &error);

            char detail[256] = {};
            std::snprintf(detail, sizeof(detail),
                          "输入 %zu 字节（构造方式 %s）；code=%s offset=%u line=%u column=%u；"
                          "契约：位置字段保持默认值，不进入解析",
                          kHugeSize, mode, ID::Json::to_string(code), error.offset, error.line, error.column);
            ID::Bench::Check(code == JsonError::input_too_large, "input_too_large.code", detail);
            ID::Bench::Check(error.offset == 0 && error.line == 1 && error.column == 1,
                             "input_too_large.default_position",
                             "README §6：input_too_large 的位置字段保持默认值（32 位偏移无法表达该范围）");
            ID::Bench::Check(document.root().is_null(), "input_too_large.root_null",
                             "拒绝超大输入时文档同样被置空");
        }

        ::VirtualFree(base, 0, MEM_RELEASE);
#endif
    }

    // ==================== 错误日志桥 ====================

    void CaseErrorLogBridge() noexcept
    {
        const std::string log_path = ID::Bench::TempPath("idjson_bridge.log");
        RemoveFileQuiet(log_path);

        const bool log_enabled = std::string_view(BENCHMARK_IDJSON_LOG) != "OFF";

        ID::Log::init();
        const ID::Log::LoggerHandle handle = ID::Log::create_logger("idjson-bridge");
        ID::Log::Logger*            logger = ID::Log::get_logger(handle);
        ID::Bench::Check(logger != nullptr, "bridge.logger_created", "IDLog 日志器创建成功（桥接前置）");
        if (logger == nullptr)
        {
            ID::Log::shutdown();
            return;
        }

        // 终端目标关掉：本设施的输出去向必须只有报告通道与文件，不能往真实控制台写日志
        logger->disable_sink(ID::Log::SinkType::Console);
        logger->set_sink_level(ID::Log::SinkType::File, ID::Log::Level::Trace);
        ID::Bench::Check(logger->set_sink_path(ID::Log::SinkType::File, log_path.c_str()),
                         "bridge.file_target_ready", "文件目标启用成功（桥接前置）");

        ID::Json::attach_error_logger(logger, "idjson-bench");

        JsonDocument   document;
        JsonParseError error{};
        const std::string bad_text = "{\n  \"a\": 1,\n  \"b\": ,\n}";
        ID::Bench::Check(parse(document, bad_text, &error) == JsonError::unexpected_token, "bridge.bad_parse",
                         "多行坏 JSON 解析失败（unexpected_token）");

        std::string content;
        const bool  read_after_attach = ReadWholeFile(log_path, content);

        if (log_enabled)
        {
            ID::Bench::Check(read_after_attach, "bridge.log_file_exists",
                             "IDJSON_ENABLE_LOG=ON 时错误日志桥写出日志文件");
            ID::Bench::Check(AnyLineContainsAll(content, {"[IDJson]", "idjson-bench", "code=unexpected_token",
                                                          "offset=19", "line=3", "column=8"}),
                             "bridge.log_line_contents",
                             "日志行含源名、错误码与位置（README §1：源名、错误码、行、列）");
        }
        else
        {
            ID::Bench::Check(!read_after_attach || CountLines(content) == 0, "bridge.off_no_log_file",
                             "IDJSON_ENABLE_LOG=OFF：不产生任何日志行（零日志文件：文件不存在或为空）");
        }

        // detach 之后零新增
        ID::Json::detach_error_logger();
        const std::size_t lines_before_detach = read_after_attach ? CountLines(content) : 0;

        JsonDocument   detached_document;
        JsonParseError detached_error{};
        ID::Bench::Check(parse(detached_document, bad_text, &detached_error) == JsonError::unexpected_token,
                         "bridge.after_detach_bad_parse", "detach 之后同一坏 JSON 仍然解析失败");

        std::string after_detach;
        const bool  read_after_detach = ReadWholeFile(log_path, after_detach);
        const std::size_t lines_after_detach = read_after_detach ? CountLines(after_detach) : 0;
        ID::Bench::Check(lines_after_detach == lines_before_detach, "bridge.after_detach_no_new_lines",
                         "detach 之后不再产生日志行（零新增）");

        // attach(nullptr, ...) 等价于 detach（公开头契约）
        ID::Json::attach_error_logger(nullptr, "idjson-bench-null");
        JsonDocument   null_document;
        ID::Bench::Check(parse(null_document, bad_text) == JsonError::unexpected_token,
                         "bridge.attach_null_bad_parse", "attach(nullptr) 之后解析照常失败");
        std::string after_null_attach;
        const bool  read_after_null_attach = ReadWholeFile(log_path, after_null_attach);
        const std::size_t lines_after_null = read_after_null_attach ? CountLines(after_null_attach) : 0;
        ID::Bench::Check(lines_after_null == lines_after_detach, "bridge.attach_null_no_new_lines",
                         "attach(nullptr) 等价于 detach：不产生新增日志");

        // 收尾：先 detach 再销毁日志器（契约要求）
        ID::Json::detach_error_logger();
        ID::Log::destroy_logger(handle);
        ID::Log::shutdown();
    }

    // ==================== 覆盖登记 ====================

    void CaseCoverageRegistration() noexcept
    {
        // Skip 只用于"契约明确非目标/平台不适用"（README §1「不提供的能力」）
        ID::Bench::Skip("api.utf8_validation",
                        "契约明确非目标（README §8.1）：解析与序列化按字节透传 >= 0x20，不校验 UTF-8");
        ID::Bench::Skip("api.in_place_mutation",
                        "契约明确非目标（README §8.4）：JsonValue 是只读视图，不提供原地修改");
        ID::Bench::Skip("api.streaming_parse",
                        "契约明确非目标（README §1）：不提供流式/增量解析");
        ID::Bench::Skip("api.single_value_free",
                        "契约明确非目标（README §1）：不提供单值释放，全部存储由 JsonDocument 一次清理");

        // Uncovered：应该覆盖、但本轮有客观障碍
        ID::Bench::Uncovered("serialize.too_deep",
                             "解析产出的值最深 255 层（kMaxJsonDepth 契约），达不到 kMaxJsonWriteDepth=256，"
                             "合法值无法触达 too_deep；构造更深的值得依赖 Detail::ArrayRef 的实现布局，"
                             "而 README §2.2 明确 Detail:: 不构成消费者契约且随实现变更 → "
                             "不改被测实现、不依赖非契约布局就没有受控取证手段");
        ID::Bench::Uncovered("parse.null_input_length_zero",
                             "空 string_view 的 data() 允许为 nullptr，而 empty 输入的期望错误位置依赖 begin 指针；"
                             "公开头未规定 data()==nullptr 且 size()==0 这一退化入参的语义（README 只描述文本解析），"
                             "强行构造等于自造未定义语义");
    }
} // namespace

namespace ID::Bench
{
    void IdJsonCorrectnessTest() noexcept
    {
        // C1/C2/C7：解析正例、边界与数据完整性
        CaseParsePositiveMatrix();
        CaseEscapeTable();
        CaseStringBoundaries();
        CaseNumberBoundaries();
        CaseDepthBoundary();

        // C3：非法输入（每个 JsonError 取值逐条触达）
        CaseUnexpectedToken();
        CaseStringErrors();
        CaseNumberErrors();
        CaseTrailingContent();
        CaseErrorPositions();
        CaseErrorCodeTexts();

        // C4：失败语义与文档状态
        CaseFailureSemantics();

        // C7：序列化
        CaseSerializeCompact();
        CaseSerializeIndent();
        CaseSerializeBufferContract();
        CaseSerializeDoubleRoundTrip();
        CaseSerializeNonFinite();

        // C5：并发契约
        CaseConcurrency();

        // C6：内存安全与统计口径
        CaseArenaAccounting();
        CaseOversizeBlockReturn();
        CaseOutOfMemory();
        CaseDefaultSegmentSource();

        // C2/C6：大输入跨巨段
        CaseLargeInputs();

        // C3：4 GiB 输入拒绝
        CaseInputTooLarge();

        // 错误日志桥（含 IDJSON_ENABLE_LOG=OFF 变体的零日志断言）
        CaseErrorLogBridge();

        // Skip / Uncovered 登记（必须最后执行，便于读者在 result 行前看到未覆盖项）
        CaseCoverageRegistration();
    }
} // namespace ID::Bench
