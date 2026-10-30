#include "IDJsonParser.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <system_error>

#include "IDJsonDetail.hpp"

/**
 *  严格 RFC 8259 的递归下降解析器实现。
 *
 *  实现说明（为什么这样写）：
 *  - 热路径只做廉价操作：空白判断走 256 项查表，无转义字符串用 memchr 一次定界，
 *    数字用 std::from_chars 直接转换，标量与短字符串完全不分配。
 *  - 转义字符串走"两遍"：第一遍校验并算出解码后的字节数，第二遍按精确尺寸写入
 *    （≤ 7 字节写栈上暂存，> 7 字节写 arena）。两遍换来的是"只分配一次、不预留多余容量"。
 *  - 行/列只在失败冷路径上统计（memchr 数换行），成功路径不承担任何位置统计成本。
 *  - arena 分配失败一律映射成 JsonError::out_of_memory，不做缩容重试：文档已经被部分填充，
 *    继续解析只会放大不一致。
 */

namespace
{
    using ID::Json::Detail::ArrayRef;
    using ID::Json::Detail::ObjectEntry;
    using ID::Json::Detail::ObjectRef;
    using ID::Json::Detail::StringRef;
    using ID::Json::JsonDocument;
    using ID::Json::JsonError;
    using ID::Json::JsonParseError;
    using ID::Json::JsonType;
    using ID::Json::JsonValue;
    using ID::Json::kInlineStringCapacity;
    using ID::Json::kMaxJsonDepth;

    // 聚合值的初始容量：小对象占多数，从 4 起步，按 2 倍扩容
    inline constexpr std::uint32_t kInitialAggregateCapacity = 4;

    // 对象索引的初始桶数（必须为 2 的幂）
    inline constexpr std::uint32_t kInitialBucketCapacity = 16;

    // 十六进制位查表：非法字节记 0xFF
    constexpr std::array<std::uint8_t, 256> make_hex_table() noexcept
    {
        std::array<std::uint8_t, 256> table{};
        for (std::size_t index = 0; index < table.size(); ++index)
        {
            table[index] = 0xFF;
        }
        for (std::uint8_t digit = 0; digit <= 9; ++digit)
        {
            table[static_cast<std::size_t>('0') + digit] = digit;
        }
        for (std::uint8_t digit = 0; digit <= 5; ++digit)
        {
            table[static_cast<std::size_t>('a') + digit] = static_cast<std::uint8_t>(10 + digit);
            table[static_cast<std::size_t>('A') + digit] = static_cast<std::uint8_t>(10 + digit);
        }
        return table;
    }

    // RFC 8259 只允许这四个空白字符
    constexpr std::array<bool, 256> make_whitespace_table() noexcept
    {
        std::array<bool, 256> table{};
        table[static_cast<std::size_t>(' ')]  = true;
        table[static_cast<std::size_t>('\t')] = true;
        table[static_cast<std::size_t>('\n')] = true;
        table[static_cast<std::size_t>('\r')] = true;
        return table;
    }

    constexpr std::array<std::uint8_t, 256> kHexValue    = make_hex_table();
    constexpr std::array<bool, 256>         kIsWhitespace = make_whitespace_table();

    ID_FORCEINLINE bool is_digit(const char ch) noexcept
    {
        return ch >= '0' && ch <= '9';
    }

    // 解码结果的字节数：≤ 7 时会写到下面的 scratch，> 7 时在 arena 上分配
    struct DecodedString
    {
        const char*   data{nullptr};                        // 字符数据
        std::uint32_t length{0};                            // 字符字节数
        bool          is_local{false};                      // data 指向本结构的 scratch
        bool          is_owned{false};                      // data 已在 arena 上
        char          scratch[kInlineStringCapacity]{};     // 短串解码暂存，避免为 ≤ 7 字节分配
    };

    ID_FORCEINLINE std::uint32_t utf8_encoded_length(const std::uint32_t code_point) noexcept
    {
        if (code_point < 0x80)
        {
            return 1;
        }
        if (code_point < 0x800)
        {
            return 2;
        }
        if (code_point < 0x10000)
        {
            return 3;
        }
        return 4;
    }

    ID_FORCEINLINE std::uint32_t encode_utf8(const std::uint32_t code_point, char* const destination) noexcept
    {
        if (code_point < 0x80)
        {
            destination[0] = static_cast<char>(code_point);
            return 1;
        }
        if (code_point < 0x800)
        {
            destination[0] = static_cast<char>(0xC0 | (code_point >> 6));
            destination[1] = static_cast<char>(0x80 | (code_point & 0x3F));
            return 2;
        }
        if (code_point < 0x10000)
        {
            destination[0] = static_cast<char>(0xE0 | (code_point >> 12));
            destination[1] = static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
            destination[2] = static_cast<char>(0x80 | (code_point & 0x3F));
            return 3;
        }
        destination[0] = static_cast<char>(0xF0 | ((code_point >> 18) & 0x07));
        destination[1] = static_cast<char>(0x80 | ((code_point >> 12) & 0x3F));
        destination[2] = static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
        destination[3] = static_cast<char>(0x80 | (code_point & 0x3F));
        return 4;
    }

    class Parser
    {
    public:
        Parser(JsonDocument& document, const std::string_view text, const bool owning) noexcept
            : m_document(document)
            , m_begin(text.data())
            , m_cursor(text.data())
            , m_end(text.data() != nullptr ? text.data() + text.size() : nullptr)
            , m_owning(owning)
        {
        }

        // 解析整个输入：一个根值 + 只允许尾随空白
        JsonError run(JsonValue& out_root) noexcept
        {
            if (m_begin == nullptr || m_cursor == m_end)
            {
                return fail(JsonError::unexpected_token, m_begin);
            }

            skip_whitespace();

            JsonValue root{};
            const JsonError error = parse_value(root, 0);
            if (error != JsonError::none)
            {
                return error;
            }

            skip_whitespace();
            if (m_cursor != m_end)
            {
                return fail(JsonError::trailing_content, m_cursor);
            }

            out_root = root;
            return JsonError::none;
        }

        const char* error_position() const noexcept { return m_error_position; }

    private:
        JsonError parse_value(JsonValue& out, const std::uint32_t depth) noexcept
        {
            if (m_cursor == m_end)
            {
                return fail(JsonError::unexpected_token, m_cursor);
            }

            switch (*m_cursor)
            {
                case '{':   return parse_object(out, depth);
                case '[':   return parse_array(out, depth);
                case '"':   return parse_string_value(out);
                case 't':   return parse_literal(out, "true", JsonType::Bool, 1);
                case 'f':   return parse_literal(out, "false", JsonType::Bool, 0);
                case 'n':   return parse_literal(out, "null", JsonType::Null, 0);
                default:    break;
            }

            if (*m_cursor == '-' || is_digit(*m_cursor))
            {
                return parse_number(out);
            }
            return fail(JsonError::unexpected_token, m_cursor);
        }

        JsonError parse_literal(JsonValue& out, const char* const literal, const JsonType type,
                               const std::uint64_t value) noexcept
        {
            const std::size_t length = std::strlen(literal);
            const std::size_t remain = static_cast<std::size_t>(m_end - m_cursor);
            if (remain < length || std::memcmp(m_cursor, literal, length) != 0)
            {
                return fail(JsonError::unexpected_token, m_cursor);
            }

            m_cursor += length;
            out.m_type         = type;
            out.m_payload.u64  = value;
            return JsonError::none;
        }

        JsonError parse_number(JsonValue& out) noexcept
        {
            const char* start = m_cursor;
            const char* scan  = start;

            if (scan < m_end && *scan == '-')
            {
                ++scan;
            }
            if (scan == m_end)
            {
                return fail(JsonError::invalid_number, scan);
            }

            if (*scan == '0')
            {
                ++scan;
                // 前导零非法："01"、"00" 都不是合法数字（RFC 8259 只允许单个 0 开头）
                if (scan < m_end && is_digit(*scan))
                {
                    return fail(JsonError::invalid_number, scan);
                }
            }
            else if (is_digit(*scan))
            {
                while (scan < m_end && is_digit(*scan))
                {
                    ++scan;
                }
            }
            else
            {
                return fail(JsonError::invalid_number, scan);
            }

            bool is_floating = false;
            if (scan < m_end && *scan == '.')
            {
                is_floating = true;
                ++scan;
                if (scan == m_end || !is_digit(*scan))
                {
                    return fail(JsonError::invalid_number, scan);
                }
                while (scan < m_end && is_digit(*scan))
                {
                    ++scan;
                }
            }
            if (scan < m_end && (*scan == 'e' || *scan == 'E'))
            {
                is_floating = true;
                ++scan;
                if (scan < m_end && (*scan == '+' || *scan == '-'))
                {
                    ++scan;
                }
                if (scan == m_end || !is_digit(*scan))
                {
                    return fail(JsonError::invalid_number, scan);
                }
                while (scan < m_end && is_digit(*scan))
                {
                    ++scan;
                }
            }

            const char* const number_end = scan;

            if (!is_floating)
            {
                std::int64_t value = 0;
                const auto result  = std::from_chars(start, number_end, value);
                if (result.ec == std::errc{} && result.ptr == number_end)
                {
                    out.m_type        = JsonType::Int;
                    out.m_payload.u64 = static_cast<std::uint64_t>(value);
                    m_cursor          = number_end;
                    return JsonError::none;
                }
                // 整数溢出（或实现拒绝）：退化为 double，仍要能精确读回才接受
            }

            double value = 0.0;
            const auto result = std::from_chars(start, number_end, value);
            if (result.ec != std::errc{} || result.ptr != number_end)
            {
                return fail(JsonError::invalid_number, start);
            }

            out.m_type       = JsonType::Double;
            out.m_payload.d  = value;
            m_cursor         = number_end;
            return JsonError::none;
        }

        JsonError parse_string_value(JsonValue& out) noexcept
        {
            DecodedString decoded;
            const JsonError error = parse_string_raw(decoded);
            if (error != JsonError::none)
            {
                return error;
            }

            if (decoded.length <= kInlineStringCapacity)
            {
                // 短串内联：连 arena 上的描述块都不需要
                out.m_type = JsonType::InlineString;
                if (decoded.length != 0)
                {
                    std::memcpy(out.m_payload.chars, decoded.data, decoded.length);
                }
                out.m_payload.chars[kInlineStringCapacity] = static_cast<char>(decoded.length);
                return JsonError::none;
            }

            auto* ref = m_document.arena().allocate_array<StringRef>(1);
            if (ref == nullptr)
            {
                return fail(JsonError::out_of_memory, m_cursor);
            }
            ref->data   = decoded.data;
            ref->length = decoded.length;

            out.m_type        = JsonType::String;
            out.m_payload.ptr = ref;
            return JsonError::none;
        }

        // 游标在开引号上；成功后游标移到闭引号之后
        JsonError parse_string_raw(DecodedString& out) noexcept
        {
            const char* const text_start = m_cursor + 1;

            // 快路径：memchr 定界，再确认这一段里没有转义与非法控制字符
            const char* const close = static_cast<const char*>(
                std::memchr(text_start, '"', static_cast<std::size_t>(m_end - text_start)));
            if (close != nullptr)
            {
                const char* scan    = text_start;
                bool        escaped = false;
                while (scan < close)
                {
                    const std::uint8_t ch = static_cast<std::uint8_t>(*scan);
                    if (ch < 0x20)
                    {
                        return fail(JsonError::control_char_in_string, scan);
                    }
                    if (ch == '\\')
                    {
                        escaped = true;
                        break;
                    }
                    ++scan;
                }

                if (!escaped)
                {
                    const auto length = static_cast<std::uint32_t>(close - text_start);
                    m_cursor = close + 1;

                    out.data     = text_start;
                    out.length   = length;
                    out.is_local = false;
                    out.is_owned = false;

                    // owning 模式：长串深拷贝进 arena（短串由调用方内联，不复制）；
                    // non_owning 模式：长串直接引用输入缓冲（零拷贝）
                    if (m_owning && length > kInlineStringCapacity)
                    {
                        char* copy = static_cast<char*>(m_document.arena().allocate(length, 1));
                        if (copy == nullptr)
                        {
                            return fail(JsonError::out_of_memory, m_cursor);
                        }
                        std::memcpy(copy, text_start, length);
                        out.data     = copy;
                        out.is_owned = true;
                    }
                    return JsonError::none;
                }
            }

            return parse_string_slow(out);
        }

        // 含转义（或未闭合）的字符串：第一遍校验并算长度，第二遍精确写入
        JsonError parse_string_slow(DecodedString& out) noexcept
        {
            const char*   scan    = m_cursor + 1;
            std::uint32_t decoded = 0;

            for (;;)
            {
                if (scan == m_end)
                {
                    return fail(JsonError::unexpected_token, scan);
                }

                const std::uint8_t ch = static_cast<std::uint8_t>(*scan);
                if (ch == '"')
                {
                    break;
                }
                if (ch < 0x20)
                {
                    return fail(JsonError::control_char_in_string, scan);
                }
                if (ch != '\\')
                {
                    ++scan;
                    ++decoded;
                    continue;
                }

                ++scan;     // 跳过 '\\'
                if (scan == m_end)
                {
                    return fail(JsonError::unexpected_token, scan);
                }

                switch (*scan)
                {
                    case '"':
                    case '\\':
                    case '/':
                    case 'b':
                    case 'f':
                    case 'n':
                    case 'r':
                    case 't':
                        ++decoded;
                        ++scan;
                        break;

                    case 'u':
                    {
                        std::uint32_t code_point = 0;
                        JsonError     error      = read_code_unit(scan, code_point);
                        if (error != JsonError::none)
                        {
                            return error;
                        }

                        if (code_point >= 0xD800 && code_point <= 0xDBFF)
                        {
                            // 高代理：后面必须紧跟 \uDC00-\uDFFF，否则是孤立的 UTF-16 代理
                            const char* const surrogate_start = scan;
                            // 用剩余字节数判断"后面至少还有 2 字节"：m_end 已经是尾后指针，
                            // 再写 scan + 1 会算出不存在的地址（形式上的 UB），语义与此处完全等价
                            if (m_end - scan < 2 || scan[0] != '\\' || scan[1] != 'u')
                            {
                                return fail(JsonError::invalid_utf16_surrogate, surrogate_start);
                            }

                            const char*   low_scan = scan + 1;
                            std::uint32_t low      = 0;
                            error = read_code_unit(low_scan, low);
                            if (error != JsonError::none || low < 0xDC00 || low > 0xDFFF)
                            {
                                return fail(JsonError::invalid_utf16_surrogate, surrogate_start);
                            }

                            code_point = 0x10000u + ((code_point - 0xD800u) << 10) + (low - 0xDC00u);
                            scan       = low_scan;
                        }
                        else if (code_point >= 0xDC00 && code_point <= 0xDFFF)
                        {
                            // 孤立低代理
                            return fail(JsonError::invalid_utf16_surrogate, scan - 4);
                        }

                        decoded += utf8_encoded_length(code_point);
                        break;
                    }

                    default:
                        return fail(JsonError::invalid_string_escape, scan);
                }
            }

            const char* const close = scan;

            char* destination = nullptr;
            if (decoded <= kInlineStringCapacity)
            {
                destination  = out.scratch;
                out.is_local = true;
            }
            else
            {
                destination = static_cast<char*>(m_document.arena().allocate(decoded, 1));
                if (destination == nullptr)
                {
                    return fail(JsonError::out_of_memory, m_cursor);
                }
                out.is_local = false;
                out.is_owned = true;
            }

            char*       write = destination;
            const char* input = m_cursor + 1;
            while (input < close)
            {
                const std::uint8_t ch = static_cast<std::uint8_t>(*input);
                if (ch != '\\')
                {
                    *write++ = static_cast<char>(ch);
                    ++input;
                    continue;
                }

                ++input;    // 跳过 '\\'
                switch (*input)
                {
                    case '"':
                        *write++ = '"';
                        ++input;
                        break;
                    case '\\':
                        *write++ = '\\';
                        ++input;
                        break;
                    case '/':
                        *write++ = '/';
                        ++input;
                        break;
                    case 'b':
                        *write++ = '\b';
                        ++input;
                        break;
                    case 'f':
                        *write++ = '\f';
                        ++input;
                        break;
                    case 'n':
                        *write++ = '\n';
                        ++input;
                        break;
                    case 'r':
                        *write++ = '\r';
                        ++input;
                        break;
                    case 't':
                        *write++ = '\t';
                        ++input;
                        break;
                    case 'u':
                    {
                        std::uint32_t code_point = 0;
                        (void)read_code_unit(input, code_point);    // 第一遍已校验，这里不会失败
                        if (code_point >= 0xD800 && code_point <= 0xDBFF)
                        {
                            const char*   low_scan = input + 1;
                            std::uint32_t low      = 0;
                            (void)read_code_unit(low_scan, low);
                            code_point = 0x10000u + ((code_point - 0xD800u) << 10) + (low - 0xDC00u);
                            input      = low_scan;
                        }
                        write += encode_utf8(code_point, write);
                        break;
                    }
                    default:
                        // 第一遍已排除所有非法转义；保留防御分支以免死循环
                        return fail(JsonError::invalid_string_escape, input);
                }
            }

            out.data   = destination;
            out.length = decoded;
            m_cursor   = close + 1;
            return JsonError::none;
        }

        // 游标指向 'u'；成功后游标移到四个十六进制位之后
        JsonError read_code_unit(const char*& cursor, std::uint32_t& out_code_unit) noexcept
        {
            const char*   scan  = cursor + 1;
            std::uint32_t value = 0;
            for (int index = 0; index < 4; ++index)
            {
                if (scan == m_end)
                {
                    return fail(JsonError::invalid_string_escape, scan);
                }
                const std::uint8_t digit = kHexValue[static_cast<std::uint8_t>(*scan)];
                if (digit == 0xFF)
                {
                    return fail(JsonError::invalid_string_escape, scan);
                }
                value = (value << 4) | digit;
                ++scan;
            }

            cursor        = scan;
            out_code_unit = value;
            return JsonError::none;
        }

        JsonError parse_array(JsonValue& out, const std::uint32_t depth) noexcept
        {
            if (depth >= kMaxJsonDepth)
            {
                return fail(JsonError::depth_exceeded, m_cursor);
            }
            ++m_cursor;     // '['

            auto* ref = m_document.arena().allocate_array<ArrayRef>(1);
            if (ref == nullptr)
            {
                return fail(JsonError::out_of_memory, m_cursor);
            }
            *ref = ArrayRef{};

            skip_whitespace();
            if (m_cursor == m_end)
            {
                return fail(JsonError::unexpected_token, m_cursor);
            }
            if (*m_cursor == ']')
            {
                ++m_cursor;
                out.m_type        = JsonType::Array;
                out.m_payload.ptr = ref;
                return JsonError::none;
            }

            for (;;)
            {
                JsonValue element{};
                JsonError error = parse_value(element, depth + 1);
                if (error != JsonError::none)
                {
                    return error;
                }

                if (ref->count == ref->capacity)
                {
                    // 容量不足：换一块 2 倍大的连续块，旧块遗弃（arena 不回收单块，交给 reset()/析构）
                    const std::uint32_t new_capacity =
                        ref->capacity == 0 ? kInitialAggregateCapacity : ref->capacity * 2;
                    auto* items = m_document.arena().allocate_array<JsonValue>(new_capacity);
                    if (items == nullptr)
                    {
                        return fail(JsonError::out_of_memory, m_cursor);
                    }
                    if (ref->count != 0)
                    {
                        std::memcpy(items, ref->items,
                                    static_cast<std::size_t>(ref->count) * sizeof(JsonValue));
                    }
                    ref->items    = items;
                    ref->capacity = new_capacity;
                }

                ref->items[ref->count] = element;
                ++ref->count;

                skip_whitespace();
                if (m_cursor == m_end)
                {
                    return fail(JsonError::unexpected_token, m_cursor);
                }
                if (*m_cursor == ',')
                {
                    ++m_cursor;
                    skip_whitespace();
                    continue;
                }
                if (*m_cursor == ']')
                {
                    ++m_cursor;
                    break;
                }
                return fail(JsonError::unexpected_token, m_cursor);
            }

            out.m_type        = JsonType::Array;
            out.m_payload.ptr = ref;
            return JsonError::none;
        }

        JsonError parse_object(JsonValue& out, const std::uint32_t depth) noexcept
        {
            if (depth >= kMaxJsonDepth)
            {
                return fail(JsonError::depth_exceeded, m_cursor);
            }
            ++m_cursor;     // '{'

            auto* ref = m_document.arena().allocate_array<ObjectRef>(1);
            if (ref == nullptr)
            {
                return fail(JsonError::out_of_memory, m_cursor);
            }
            *ref = ObjectRef{};

            skip_whitespace();
            if (m_cursor == m_end)
            {
                return fail(JsonError::unexpected_token, m_cursor);
            }
            if (*m_cursor == '}')
            {
                ++m_cursor;
                out.m_type        = JsonType::Object;
                out.m_payload.ptr = ref;
                return JsonError::none;
            }

            for (;;)
            {
                skip_whitespace();
                if (m_cursor == m_end || *m_cursor != '"')
                {
                    return fail(JsonError::unexpected_token, m_cursor);
                }

                DecodedString key;
                JsonError error = parse_string_raw(key);
                if (error != JsonError::none)
                {
                    return error;
                }

                // 键统一物化到 arena（ObjectEntry 只存指针 + 长度，不内联字符）：
                // owning 模式下输入缓冲即将失效，必须复制；non_owning 模式下无转义长键可直接引用输入
                const char*   key_data   = key.length == 0 ? nullptr : key.data;
                const std::uint32_t key_length = key.length;
                if (key_length != 0 && !key.is_owned && (m_owning || key.is_local))
                {
                    char* copy = static_cast<char*>(m_document.arena().allocate(key_length, 1));
                    if (copy == nullptr)
                    {
                        return fail(JsonError::out_of_memory, m_cursor);
                    }
                    std::memcpy(copy, key.data, key_length);
                    key_data = copy;
                }

                skip_whitespace();
                if (m_cursor == m_end || *m_cursor != ':')
                {
                    return fail(JsonError::unexpected_token, m_cursor);
                }
                ++m_cursor;
                skip_whitespace();

                JsonValue value{};
                error = parse_value(value, depth + 1);
                if (error != JsonError::none)
                {
                    return error;
                }

                error = append_entry(*ref, key_data, key_length, value);
                if (error != JsonError::none)
                {
                    return error;
                }

                skip_whitespace();
                if (m_cursor == m_end)
                {
                    return fail(JsonError::unexpected_token, m_cursor);
                }
                if (*m_cursor == ',')
                {
                    ++m_cursor;
                    continue;
                }
                if (*m_cursor == '}')
                {
                    ++m_cursor;
                    break;
                }
                return fail(JsonError::unexpected_token, m_cursor);
            }

            out.m_type        = JsonType::Object;
            out.m_payload.ptr = ref;
            return JsonError::none;
        }

        JsonError append_entry(ObjectRef& ref, const char* const key, const std::uint32_t key_length,
                               const JsonValue& value) noexcept
        {
            std::uint32_t existing = 0;
            if (find_entry_index(ref, key, key_length, existing))
            {
                // 重复键 last-wins：位置保持首次插入序，值取最后一次出现的
                ref.entries[existing].value = value;
                return JsonError::none;
            }

            if (ref.count == ref.capacity)
            {
                const std::uint32_t new_capacity =
                    ref.capacity == 0 ? kInitialAggregateCapacity : ref.capacity * 2;
                auto* entries = m_document.arena().allocate_array<ObjectEntry>(new_capacity);
                if (entries == nullptr)
                {
                    return fail(JsonError::out_of_memory, m_cursor);
                }
                if (ref.count != 0)
                {
                    std::memcpy(entries, ref.entries,
                                static_cast<std::size_t>(ref.count) * sizeof(ObjectEntry));
                }
                ref.entries  = entries;
                ref.capacity = new_capacity;
            }

            ObjectEntry& entry = ref.entries[ref.count];
            entry.key          = key;
            entry.key_length   = key_length;
            entry.value        = value;
            ++ref.count;

            if (ref.count > ID::Json::Detail::kObjectIndexThreshold)
            {
                // 索引装载因子保持 ≤ 0.5：超过就重建（旧桶数组遗弃给 arena 统一回收）
                const bool need_rebuild =
                    ref.buckets == nullptr ||
                    static_cast<std::uint64_t>(ref.count) * 2 > static_cast<std::uint64_t>(ref.bucket_mask) + 1;
                if (need_rebuild)
                {
                    return rebuild_index(ref);
                }

                const std::string_view key_view =
                    key_length == 0 ? std::string_view{} : std::string_view(key, key_length);
                std::uint32_t slot = ID::Json::Detail::fnv1a(key_view) & ref.bucket_mask;
                while (ref.buckets[slot] != 0)
                {
                    slot = (slot + 1) & ref.bucket_mask;
                }
                ref.buckets[slot] = ref.count;      // 桶存"成员下标 + 1"，0 表示空桶
            }
            return JsonError::none;
        }

        bool find_entry_index(const ObjectRef& ref, const char* const key, const std::uint32_t key_length,
                              std::uint32_t& out_index) const noexcept
        {
            if (ref.buckets != nullptr)
            {
                const std::string_view key_view =
                    key_length == 0 ? std::string_view{} : std::string_view(key, key_length);
                const std::uint32_t mask = ref.bucket_mask;
                std::uint32_t       slot = ID::Json::Detail::fnv1a(key_view) & mask;
                for (;;)
                {
                    const std::uint32_t bucket = ref.buckets[slot];
                    if (bucket == 0)
                    {
                        return false;
                    }
                    const ObjectEntry& entry = ref.entries[bucket - 1];
                    if (entry.key_length == key_length &&
                        (key_length == 0 || std::memcmp(entry.key, key, key_length) == 0))
                    {
                        out_index = bucket - 1;
                        return true;
                    }
                    slot = (slot + 1) & mask;
                }
            }

            for (std::uint32_t index = 0; index < ref.count; ++index)
            {
                const ObjectEntry& entry = ref.entries[index];
                if (entry.key_length == key_length &&
                    (key_length == 0 || std::memcmp(entry.key, key, key_length) == 0))
                {
                    out_index = index;
                    return true;
                }
            }
            return false;
        }

        JsonError rebuild_index(ObjectRef& ref) noexcept
        {
            std::uint32_t bucket_count = kInitialBucketCapacity;
            while (static_cast<std::uint64_t>(bucket_count) < static_cast<std::uint64_t>(ref.count) * 2)
            {
                bucket_count <<= 1;
            }

            auto* buckets = m_document.arena().allocate_array<std::uint32_t>(bucket_count);
            if (buckets == nullptr)
            {
                return fail(JsonError::out_of_memory, m_cursor);
            }
            std::memset(buckets, 0, static_cast<std::size_t>(bucket_count) * sizeof(std::uint32_t));

            const std::uint32_t mask = bucket_count - 1;
            for (std::uint32_t index = 0; index < ref.count; ++index)
            {
                const ObjectEntry& entry = ref.entries[index];
                const std::string_view key_view =
                    entry.key_length == 0 ? std::string_view{} : std::string_view(entry.key, entry.key_length);
                std::uint32_t slot = ID::Json::Detail::fnv1a(key_view) & mask;
                while (buckets[slot] != 0)
                {
                    slot = (slot + 1) & mask;
                }
                buckets[slot] = index + 1;
            }

            ref.buckets     = buckets;
            ref.bucket_mask = mask;
            return JsonError::none;
        }

        ID_FORCEINLINE void skip_whitespace() noexcept
        {
            while (m_cursor < m_end && kIsWhitespace[static_cast<std::uint8_t>(*m_cursor)])
            {
                ++m_cursor;
            }
        }

        ID_NOINLINE JsonError fail(const JsonError code, const char* const position) noexcept
        {
            m_error_position = position != nullptr ? position : m_begin;
            return code;
        }

    private:
        JsonDocument& m_document;                           // 产出的存储来源
        const char*   m_begin{nullptr};                     // 输入起点（行/列统计要用）
        const char*   m_cursor{nullptr};                    // 当前读取位置
        const char*   m_end{nullptr};                       // 输入末尾（不含）
        const char*   m_error_position{nullptr};            // 出错位置（首个错误）
        bool          m_owning{true};                       // true=深拷贝字符串，false=引用输入
    };

    // 失败冷路径：从缓冲区起点统计行/列（成功路径不承担这个成本）
    ID_NOINLINE ID_COLD void fill_error_position(const char* const begin, const char* const end,
                                                 const char* position, JsonParseError& out) noexcept
    {
        out.offset = 0;
        out.line   = 1;
        out.column = 1;

        if (begin == nullptr || position == nullptr)
        {
            return;
        }
        if (end != nullptr && position > end)
        {
            position = end;
        }
        if (position < begin)
        {
            position = begin;
        }

        const auto offset = static_cast<std::size_t>(position - begin);
        out.offset = static_cast<std::uint32_t>(offset > 0xFFFFFFFFu ? 0xFFFFFFFFu : offset);

        std::uint32_t line       = 1;
        const char*   line_start = begin;
        const char*   scan       = begin;
        while (scan < position)
        {
            const void* const found = std::memchr(scan, '\n', static_cast<std::size_t>(position - scan));
            if (found == nullptr)
            {
                break;
            }
            scan       = static_cast<const char*>(found) + 1;
            line_start = scan;
            ++line;
        }

        const auto column = static_cast<std::size_t>(position - line_start) + 1;
        out.line   = line;
        out.column = static_cast<std::uint32_t>(column > 0xFFFFFFFFu ? 0xFFFFFFFFu : column);
    }

    // 解析主流程：条件校验、文档重置、失败置空与错误统计都在这一处，两个公开入口共用
    ID_NOINLINE JsonError parse_into(JsonDocument& document, const std::string_view text, const bool owning,
                                     JsonValue& out_root, JsonParseError* const out_error) noexcept
    {
        if (!document.is_ready())
        {
            JsonParseError error{};
            error.code = JsonError::not_initialized;
            if (out_error != nullptr)
            {
                *out_error = error;
            }
            ID::Json::Detail::log_parse_error(error);
            return JsonError::not_initialized;
        }

        // 每次解析都从干净状态开始：上一次的内容（含 non_owning 引用的输入）立即失效
        document.reset();

        // 长度计量（转义解码计数、StringRef/键长、错误偏移）都是 32 位。输入超过 4 GiB - 1 时这些
        // 计数会回绕，含转义的字符串可能按错误长度写出并越界，因此整份输入直接拒绝。
        // 32 位目标的 size_t 本身就装不下更大的输入，用 if constexpr 折掉这段，避免"恒为假"的比较。
        if constexpr (sizeof(std::size_t) > sizeof(std::uint32_t))
        {
            if (text.size() > static_cast<std::size_t>(0xFFFFFFFFu))
            {
                JsonParseError error{};
                error.code = JsonError::input_too_large;    // 位置留默认值：4 GiB 范围无法用 32 位偏移表达
                if (out_error != nullptr)
                {
                    *out_error = error;
                }
                ID::Json::Detail::log_parse_error(error);
                return JsonError::input_too_large;
            }
        }

        Parser     parser(document, text, owning);
        JsonValue  root{};
        const JsonError code = parser.run(root);
        if (code == JsonError::none)
        {
            out_root = root;
            if (out_error != nullptr)
            {
                *out_error = JsonParseError{};
            }
            return JsonError::none;
        }

        // 失败置空：根值留在文档里的半成品必须一起作废，arena 游标回退
        document.reset();

        JsonParseError error{};
        error.code = code;
        fill_error_position(text.data(), text.data() != nullptr ? text.data() + text.size() : nullptr,
                            parser.error_position(), error);
        if (out_error != nullptr)
        {
            *out_error = error;
        }
        ID::Json::Detail::log_parse_error(error);
        return code;
    }
} // namespace

namespace ID::Json
{
    JsonError parse(JsonDocument& document, const std::string_view text, JsonParseError* const out_error) noexcept
    {
        JsonValue root{};
        const JsonError code = parse_into(document, text, true, root, out_error);
        if (code == JsonError::none)
        {
            document.m_root = root;
        }
        return code;
    }

    JsonError parse_non_owning(JsonDocument& document, const std::string_view text,
                               JsonParseError* const out_error) noexcept
    {
        JsonValue root{};
        const JsonError code = parse_into(document, text, false, root, out_error);
        if (code == JsonError::none)
        {
            document.m_root = root;
        }
        return code;
    }
} // namespace ID::Json
