#include "cmake/Value/Argument.hpp"

namespace ID::CMake
{
    namespace
    {
        // 裸值允许的字符集：A-Z a-z 0-9 _ - . / : + = @ ,（不含空格、中文与 '$'）
        bool is_bare_char(char ch) noexcept
        {
            return (ch >= 'A' && ch <= 'Z')
                || (ch >= 'a' && ch <= 'z')
                || (ch >= '0' && ch <= '9')
                || ch == '_'
                || ch == '-'
                || ch == '.'
                || ch == '/'
                || ch == ':'
                || ch == '+'
                || ch == '='
                || ch == '@'
                || ch == ',';
        }

        // 加引号并转义。逐字符单次扫描：转义产出的反斜杠不再被扫描，
        // 因此不必分多轮替换，也不会出现自身产物的二次转义或替换顺序依赖。
        // escape_dollar 为 true 时把 '$' 转成字面量（Quoted），为 false 时保留变量展开（QuotedExpand）。
        std::string render_quoted(std::string_view text, bool escape_dollar)
        {
            std::string result;
            result.reserve(text.size() + 2);
            result.push_back('"');
            for (const char ch : text)
            {
                switch (ch)
                {
                    case '\\':
                        result.append("\\\\");
                        break;
                    case '"':
                        result.append("\\\"");
                        break;
                    case '$':
                        if (escape_dollar)
                        {
                            result.append("\\$");
                        }
                        else
                        {
                            result.push_back('$');
                        }
                        break;
                    default:
                        result.push_back(ch);
                        break;
                }
            }
            result.push_back('"');
            return result;
        }
    } // namespace

    Argument::Argument(std::string_view text, ArgumentKind kind)
        : m_text(text)
        , m_kind(kind)
    {
    }

    Argument Argument::bare(std::string_view text)
    {
        return Argument(text, ArgumentKind::Bare);
    }

    Argument Argument::quoted(std::string_view text)
    {
        return Argument(text, ArgumentKind::Quoted);
    }

    Argument Argument::quoted_expand(std::string_view text)
    {
        return Argument(text, ArgumentKind::QuotedExpand);
    }

    Argument Argument::verbatim(std::string_view text)
    {
        return Argument(text, ArgumentKind::Verbatim);
    }

    ArgumentKind Argument::get_kind() const noexcept
    {
        return m_kind;
    }

    std::string_view Argument::get_text() const noexcept
    {
        return m_text;
    }

    bool is_valid_bare_argument(const Argument& argument) noexcept
    {
        if (argument.get_kind() != ArgumentKind::Bare)
        {
            // 只有裸值受字符集约束；带引号的类别由渲染负责转义，Verbatim 是原始词法片段
            return true;
        }

        const std::string_view text = argument.get_text();
        if (text.empty())
        {
            return false;
        }

        for (const char ch : text)
        {
            if (!is_bare_char(ch))
            {
                return false;
            }
        }
        return true;
    }

    std::string render_argument(const Argument& argument)
    {
        const std::string_view text = argument.get_text();
        std::string result;
        switch (argument.get_kind())
        {
            case ArgumentKind::Bare:
            case ArgumentKind::Verbatim:
                // 裸值与原始词法片段都原样写出：不加引号、不转义
                result.assign(text);
                break;
            case ArgumentKind::Quoted:
                result = render_quoted(text, true);
                break;
            case ArgumentKind::QuotedExpand:
                result = render_quoted(text, false);
                break;
        }
        return result;
    }
} // namespace ID::CMake
