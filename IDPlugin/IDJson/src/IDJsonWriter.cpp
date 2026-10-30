#include "IDJsonWriter.hpp"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <system_error>

/**
 *  序列化实现：直写输出缓冲，不分配、不经 arena。
 *
 *  实现说明：
 *  - 写入路径只有"当前已写字节数 + 容量"两个状态：容量不足时继续计数但不写，因此一次遍历就能
 *    同时得到"写了什么"和"需要多少"。批量写入用 memcpy/memset 而不是逐字节循环。
 *  - 数字走 std::to_chars：整数最短十进制，double 最短往返；整数形态的 double 补 ".0" 保证
 *    round-trip 后仍是 Double 而不是 Int。
 *  - 非有限 double 直接报错：不写 null、不写 0，避免把非法数值静默变成合法但不同的值。
 *  - 缩进只影响空白，不改变语义；缩进量为 0 时输出紧凑单行。
 */

namespace
{
    using ID::Json::Detail::ObjectEntry;
    using ID::Json::JsonType;
    using ID::Json::JsonValue;
    using ID::Json::JsonWriteError;
    using ID::Json::kMaxJsonWriteDepth;

    constexpr char kHexDigits[] = "0123456789abcdef";

    struct WriteContext
    {
        char*         out{nullptr};         // 输出缓冲，可为 nullptr（只探测尺寸）
        std::size_t   capacity{0};          // 可用字节数
        std::size_t   written{0};           // 已请求写入的字节数（可能超过 capacity）
        std::uint32_t indent{0};            // 每层缩进的空格数，0 表示紧凑格式
    };

    ID_FORCEINLINE void put_char(WriteContext& context, const char ch) noexcept
    {
        if (context.out != nullptr && context.written < context.capacity)
        {
            context.out[context.written] = ch;
        }
        ++context.written;
    }

    void put_text(WriteContext& context, const char* const text, const std::size_t length) noexcept
    {
        if (context.out != nullptr && context.written < context.capacity)
        {
            const std::size_t available   = context.capacity - context.written;
            const std::size_t write_count = length < available ? length : available;
            std::memcpy(context.out + context.written, text, write_count);
        }
        context.written += length;
    }

    void put_repeat(WriteContext& context, const char ch, const std::size_t count) noexcept
    {
        if (context.out != nullptr && context.written < context.capacity)
        {
            const std::size_t available   = context.capacity - context.written;
            const std::size_t write_count = count < available ? count : available;
            std::memset(context.out + context.written, static_cast<int>(static_cast<std::uint8_t>(ch)),
                        write_count);
        }
        context.written += count;
    }

    // 换行 + 缩进：level 为下一行的层级（根值为 0 层）
    void put_line_break(WriteContext& context, const std::uint32_t level) noexcept
    {
        put_char(context, '\n');
        put_repeat(context, ' ', static_cast<std::size_t>(context.indent) * level);
    }

    void write_string(WriteContext& context, const std::string_view text) noexcept
    {
        put_char(context, '"');
        for (const char raw : text)
        {
            const auto ch = static_cast<std::uint8_t>(raw);
            switch (ch)
            {
                case '"':
                    put_char(context, '\\');
                    put_char(context, '"');
                    break;
                case '\\':
                    put_char(context, '\\');
                    put_char(context, '\\');
                    break;
                case '\b':
                    put_char(context, '\\');
                    put_char(context, 'b');
                    break;
                case '\f':
                    put_char(context, '\\');
                    put_char(context, 'f');
                    break;
                case '\n':
                    put_char(context, '\\');
                    put_char(context, 'n');
                    break;
                case '\r':
                    put_char(context, '\\');
                    put_char(context, 'r');
                    break;
                case '\t':
                    put_char(context, '\\');
                    put_char(context, 't');
                    break;
                default:
                    if (ch < 0x20)
                    {
                        // 其余控制字符写成 \u00XX，与解析器接受的转义形式对称
                        put_char(context, '\\');
                        put_char(context, 'u');
                        put_char(context, '0');
                        put_char(context, '0');
                        put_char(context, kHexDigits[ch >> 4]);
                        put_char(context, kHexDigits[ch & 0x0F]);
                    }
                    else
                    {
                        // ≥ 0x20 的字节原样透传：UTF-8 校验放在调用方，序列化不做编码转换
                        put_char(context, static_cast<char>(ch));
                    }
                    break;
            }
        }
        put_char(context, '"');
    }

    // int64 的十进制最多 20 字节（含符号），24 字节缓冲不可能失败
    void write_int64(WriteContext& context, const std::int64_t value) noexcept
    {
        char       digits[24];
        const auto result = std::to_chars(digits, digits + sizeof(digits), value);
        if (result.ec != std::errc{})
        {
            return;
        }
        put_text(context, digits, static_cast<std::size_t>(result.ptr - digits));
    }

    bool write_double(WriteContext& context, const double value) noexcept
    {
        if (!std::isfinite(value))
        {
            return false;   // NaN / ±Inf 不是合法 JSON
        }

        // 最短往返表示；40 字节覆盖 -1.7976931348623157e+308 这类最长形式
        char       buffer[40];
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
        if (result.ec != std::errc{})
        {
            return false;
        }

        std::size_t length = static_cast<std::size_t>(result.ptr - buffer);
        bool        has_fraction_or_exponent = false;
        for (std::size_t index = 0; index < length; ++index)
        {
            const char ch = buffer[index];
            if (ch == '.' || ch == 'e' || ch == 'E')
            {
                has_fraction_or_exponent = true;
                break;
            }
        }
        if (!has_fraction_or_exponent)
        {
            // 类型保真：整数形态的 double 补 ".0"，否则读回来会变成 Int
            if (length + 2 > sizeof(buffer))
            {
                return false;
            }
            buffer[length]     = '.';
            buffer[length + 1] = '0';
            length += 2;
        }

        put_text(context, buffer, length);
        return true;
    }

    JsonWriteError write_value(WriteContext& context, const JsonValue& value, const std::uint32_t depth) noexcept
    {
        switch (value.type())
        {
            case JsonType::Null:
                put_text(context, "null", 4);
                return JsonWriteError::none;

            case JsonType::Bool:
                if (value.as_bool())
                {
                    put_text(context, "true", 4);
                }
                else
                {
                    put_text(context, "false", 5);
                }
                return JsonWriteError::none;

            case JsonType::Int:
                write_int64(context, value.as_int64());
                return JsonWriteError::none;

            case JsonType::Double:
                return write_double(context, value.as_double()) ? JsonWriteError::none
                                                                : JsonWriteError::non_finite_number;

            case JsonType::String:
            case JsonType::InlineString:
                write_string(context, value.as_string());
                return JsonWriteError::none;

            case JsonType::Array:
            {
                if (depth >= kMaxJsonWriteDepth)
                {
                    return JsonWriteError::too_deep;
                }

                const std::size_t count = value.size();
                if (count == 0)
                {
                    put_text(context, "[]", 2);
                    return JsonWriteError::none;
                }

                put_char(context, '[');
                for (std::size_t index = 0; index < count; ++index)
                {
                    if (index != 0)
                    {
                        put_char(context, ',');
                    }
                    if (context.indent != 0)
                    {
                        put_line_break(context, depth + 1);
                    }
                    const JsonWriteError error = write_value(context, value[index], depth + 1);
                    if (error != JsonWriteError::none)
                    {
                        return error;
                    }
                }
                if (context.indent != 0)
                {
                    put_line_break(context, depth);
                }
                put_char(context, ']');
                return JsonWriteError::none;
            }

            case JsonType::Object:
            {
                if (depth >= kMaxJsonWriteDepth)
                {
                    return JsonWriteError::too_deep;
                }

                if (value.size() == 0)
                {
                    put_text(context, "{}", 2);
                    return JsonWriteError::none;
                }

                put_char(context, '{');
                bool is_first = true;
                for (const ObjectEntry* entry = value.members_begin(); entry != value.members_end(); ++entry)
                {
                    if (!is_first)
                    {
                        put_char(context, ',');
                    }
                    is_first = false;

                    if (context.indent != 0)
                    {
                        put_line_break(context, depth + 1);
                    }

                    const std::string_view key =
                        entry->key_length == 0 ? std::string_view{} : std::string_view(entry->key, entry->key_length);
                    write_string(context, key);
                    put_char(context, ':');
                    if (context.indent != 0)
                    {
                        put_char(context, ' ');
                    }

                    const JsonWriteError error = write_value(context, entry->value, depth + 1);
                    if (error != JsonWriteError::none)
                    {
                        return error;
                    }
                }
                if (context.indent != 0)
                {
                    put_line_break(context, depth);
                }
                put_char(context, '}');
                return JsonWriteError::none;
            }
        }

        // 所有 JsonType 取值都已覆盖；保留返回值以满足"函数必须有返回"的形式要求
        return JsonWriteError::none;
    }
} // namespace

namespace ID::Json
{
    std::size_t serialize(const JsonValue& root, char* const out, const std::size_t capacity,
                          const JsonWriteOptions& options, JsonWriteError* const err) noexcept
    {
        WriteContext context{};
        context.out      = out;
        context.capacity = capacity;
        context.indent   = options.indent;

        const JsonWriteError error = write_value(context, root, 0);
        if (error != JsonWriteError::none)
        {
            if (err != nullptr)
            {
                *err = error;
            }
            return 0;
        }

        if (err != nullptr)
        {
            *err = JsonWriteError::none;
        }
        if (out != nullptr && context.written < capacity)
        {
            out[context.written] = '\0';
        }
        return context.written;
    }
} // namespace ID::Json
