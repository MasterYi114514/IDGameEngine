#include "project/Base/Text.hpp"

#include <algorithm>
#include <cstddef>

#include "Core/Core.hpp"

/**
 *  文本工具实现：所有判断与转换都按字节处理，不读 locale、不做 Unicode 规范化。
 */

namespace ID::Project
{
    namespace
    {
        ID_FORCEINLINE bool is_ascii_space(char unit) noexcept
        {
            return unit == ' ' || unit == '\t' || unit == '\r' || unit == '\n' || unit == '\v' || unit == '\f';
        }

        ID_FORCEINLINE char to_ascii_upper(char unit) noexcept
        {
            return (unit >= 'a' && unit <= 'z') ? static_cast<char>(unit - 'a' + 'A') : unit;
        }

        ID_FORCEINLINE char to_ascii_lower(char unit) noexcept
        {
            return (unit >= 'A' && unit <= 'Z') ? static_cast<char>(unit - 'A' + 'a') : unit;
        }

        inline constexpr std::string_view kHexDigits = "0123456789abcdef";

        // 十六进制展开到定长缓冲，避免逐字符 push_back 带来的多次扩容
        void append_hex_byte(std::string& target, std::uint8_t value)
        {
            target.push_back(kHexDigits[static_cast<std::size_t>(value >> 4)]);
            target.push_back(kHexDigits[static_cast<std::size_t>(value & 0x0F)]);
        }

        void append_unsigned(std::string& target, std::uint64_t value)
        {
            char              buffer[20] = {};
            std::size_t       length     = 0;

            do
            {
                buffer[length] = static_cast<char>('0' + static_cast<char>(value % 10));
                ++length;
                value /= 10;
            } while (value != 0);

            // 逐位倒着写回，得到正序数字串
            for (std::size_t index = length; index > 0; --index)
            {
                target.push_back(buffer[index - 1]);
            }
        }
    } // namespace

    bool starts_with(std::string_view text, std::string_view prefix) noexcept
    {
        return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
    }

    bool ends_with(std::string_view text, std::string_view suffix) noexcept
    {
        return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    std::string_view trim_ascii_space(std::string_view text) noexcept
    {
        std::size_t begin = 0;
        std::size_t end   = text.size();

        while (begin < end && is_ascii_space(text[begin]))
        {
            ++begin;
        }
        while (end > begin && is_ascii_space(text[end - 1]))
        {
            --end;
        }

        return text.substr(begin, end - begin);
    }

    std::string ascii_upper(std::string_view text)
    {
        std::string result(text);
        for (char& unit : result)
        {
            unit = to_ascii_upper(unit);
        }
        return result;
    }

    std::string ascii_lower(std::string_view text)
    {
        std::string result(text);
        for (char& unit : result)
        {
            unit = to_ascii_lower(unit);
        }
        return result;
    }

    bool ascii_iequals(std::string_view left, std::string_view right) noexcept
    {
        if (left.size() != right.size())
        {
            return false;
        }

        for (std::size_t index = 0; index < left.size(); ++index)
        {
            if (left[index] == right[index])
            {
                continue;
            }
            if (to_ascii_lower(left[index]) != to_ascii_lower(right[index]))
            {
                return false;
            }
        }
        return true;
    }

    std::string to_decimal(std::uint64_t value)
    {
        std::string result;
        result.reserve(20);
        append_unsigned(result, value);
        return result;
    }

    std::string to_signed_decimal(std::int64_t value)
    {
        std::string result;
        result.reserve(21);

        if (value < 0)
        {
            result.push_back('-');
            // 先取绝对值再转换：最小值取反会溢出，用无符号减法避开
            append_unsigned(result, static_cast<std::uint64_t>(0) - static_cast<std::uint64_t>(value));
            return result;
        }

        append_unsigned(result, static_cast<std::uint64_t>(value));
        return result;
    }

    std::string to_lower_hex(std::span<const std::uint8_t> bytes)
    {
        std::string result;
        result.reserve(bytes.size() * 2);

        for (const std::uint8_t byte : bytes)
        {
            append_hex_byte(result, byte);
        }
        return result;
    }

    std::string quote_for_display(std::string_view text)
    {
        std::string result;
        result.reserve(text.size() + 2);
        result.push_back('"');

        for (const char raw : text)
        {
            const auto byte = static_cast<unsigned char>(raw);
            switch (byte)
            {
                case '\\':  result.append("\\\\"); break;
                case '"':   result.append("\\\""); break;
                case '\t':  result.append("\\t");  break;
                case '\r':  result.append("\\r");  break;
                case '\n':  result.append("\\n");  break;
                default:
                {
                    if (byte < 0x20 || byte == 0x7F)
                    {
                        result.append("\\x");
                        append_hex_byte(result, byte);
                        break;
                    }
                    result.push_back(raw);
                    break;
                }
            }
        }

        result.push_back('"');
        return result;
    }

    std::string join_for_display(std::span<const std::string> arguments)
    {
        std::string result;
        for (const std::string& argument : arguments)
        {
            if (!result.empty())
            {
                result.push_back(' ');
            }
            result.append(quote_for_display(argument));
        }
        return result;
    }

    bool contains_control_character(std::string_view text) noexcept
    {
        for (const char raw : text)
        {
            const auto byte = static_cast<unsigned char>(raw);
            if (byte == 0x7F)
            {
                return true;
            }
            if (byte >= 0x20)
            {
                continue;
            }
            if (byte != '\r' && byte != '\n' && byte != '\t')
            {
                return true;
            }
        }
        return false;
    }

    bool is_valid_posix_relative(std::string_view text) noexcept
    {
        if (text.empty() || text.front() == '/')
        {
            return false;
        }

        for (const char raw : text)
        {
            if (raw == '\\')
            {
                return false;
            }
        }

        // 盘符形态 "X:" 与 UNC 形态都被上面的前导 '/' 判断覆盖；这里只挡盘符
        if (text.size() >= 2 && text[1] == ':')
        {
            const char first = text[0];
            const bool is_letter = (first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z');
            if (is_letter)
            {
                return false;
            }
        }
        return true;
    }

    std::string escape_cmake_string(std::string_view text)
    {
        std::string result;
        result.reserve(text.size());

        for (const char unit : text)
        {
            if (unit == '\\' || unit == '"')
            {
                result.push_back('\\');
            }
            result.push_back(unit);
        }
        return result;
    }
} // namespace ID::Project
