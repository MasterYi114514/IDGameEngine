#include "JsonParser.hpp"
#include "Arena.hpp"
#include "ArenaAllocator.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ID
{
    // ═══════════════════════════════════════════
    //  JsonParser — 递归下降 JSON 解析器
    //
    //  内部实现，通过 JSON::parse() 公开接口调用。
    //  所有字符串和容器内存均从指定 Arena 分配。
    // ═══════════════════════════════════════════
    class JsonParser
    {
    public:
        JsonParser(std::string_view source, ArenaID arena_id)
            : m_src(source)
            , m_arena_id(arena_id)
            , m_pos(0)
        {}

        Json parse_value();

    private:
        // ── 词法辅助 ──
        char peek() const;
        char advance();
        void skip_whitespace();
        bool at_end() const { return m_pos >= m_src.size(); }

        // ── 各类型解析 ──
        Json parse_null();
        Json parse_bool();
        Json parse_number();
        Json parse_string();
        Json parse_array();
        Json parse_object();

        // ── 报错 ──
        [[noreturn]] void error(const char* msg) const;

        // ── 成员 ──
        std::string_view m_src;
        ArenaID          m_arena_id;
        size_t           m_pos;
    };

    // ═══════════════════════════════════════════
    //  词法辅助
    // ═══════════════════════════════════════════

    char JsonParser::peek() const
    {
        if(at_end()) return '\0';
        return m_src[m_pos];
    }

    char JsonParser::advance()
    {
        if(at_end()) return '\0';
        return m_src[m_pos++];
    }

    void JsonParser::skip_whitespace()
    {
        while(!at_end())
        {
            char c = peek();
            if(c == ' ' || c == '\t' || c == '\n' || c == '\r')
            {
                ++m_pos;
            }
            else
            {
                break;
            }
        }
    }

    [[noreturn]] void JsonParser::error(const char* msg) const
    {
        // 计算当前位置附近的行列信息便于调试
        size_t line = 1;
        size_t col  = 1;
        for(size_t i = 0; i < m_pos && i < m_src.size(); ++i)
        {
            if(m_src[i] == '\n') { ++line; col = 1; }
            else { ++col; }
        }

        char buf[256];
        std::snprintf(buf, sizeof(buf),
                      "JsonParser error at line %zu, column %zu: %s",
                      line, col, msg);
        throw std::runtime_error(buf);
    }

    // ═══════════════════════════════════════════
    //  parse_value — 分发入口
    // ═══════════════════════════════════════════

    Json JsonParser::parse_value()
    {
        skip_whitespace();

        if(at_end())
            error("unexpected end of input");

        char c = peek();

        switch(c)
        {
        case 'n': return parse_null();
        case 't': case 'f': return parse_bool();
        case '"': return parse_string();
        case '[': return parse_array();
        case '{': return parse_object();
        default:
            if(c == '-' || (c >= '0' && c <= '9'))
                return parse_number();
            error("unexpected character");
        }
    }

    // ═══════════════════════════════════════════
    //  parse_null
    // ═══════════════════════════════════════════

    Json JsonParser::parse_null()
    {
        if(m_src.substr(m_pos, 4) != "null")
            error("expected 'null'");

        m_pos += 4;
        return Json{};   // 默认构造 = Null
    }

    // ═══════════════════════════════════════════
    //  parse_bool
    // ═══════════════════════════════════════════

    Json JsonParser::parse_bool()
    {
        if(m_src.substr(m_pos, 4) == "true")
        {
            m_pos += 4;
            return Json(true);
        }

        if(m_src.substr(m_pos, 5) == "false")
        {
            m_pos += 5;
            return Json(false);
        }

        error("expected 'true' or 'false'");
    }

    // ═══════════════════════════════════════════
    //  parse_number — 支持整数与浮点数
    //
    //  策略：先收集完整数字字面量，然后用 strtod
    //  判断是否为整数（无小数点、无指数）。
    // ═══════════════════════════════════════════

    Json JsonParser::parse_number()
    {
        size_t start = m_pos;

        // 可选负号
        if(peek() == '-')
            advance();

        // 整数部分
        if(peek() == '0')
        {
            advance();
        }
        else if(peek() >= '1' && peek() <= '9')
        {
            while(peek() >= '0' && peek() <= '9')
                advance();
        }
        else
        {
            error("expected digit in number");
        }

        bool is_float = false;

        // 小数部分
        if(peek() == '.')
        {
            is_float = true;
            advance();

            if(peek() < '0' || peek() > '9')
                error("expected digit after decimal point");

            while(peek() >= '0' && peek() <= '9')
                advance();
        }

        // 指数部分
        if(peek() == 'e' || peek() == 'E')
        {
            is_float = true;
            advance();

            if(peek() == '+' || peek() == '-')
                advance();

            if(peek() < '0' || peek() > '9')
                error("expected digit in exponent");

            while(peek() >= '0' && peek() <= '9')
                advance();
        }

        // 取出数字字面量
        std::string_view num_sv = m_src.substr(start, m_pos - start);

        if(is_float)
        {
            char* end  = nullptr;
            double val = std::strtod(num_sv.data(), &end);
            if(end != num_sv.data() + num_sv.size())
                error("invalid number literal");
            return Json(val);
        }
        else
        {
            char*  end = nullptr;
            int64_t val = std::strtoll(num_sv.data(), &end, 10);
            if(end != num_sv.data() + num_sv.size())
                error("invalid integer literal");

            // 检查是否溢出 int32_t
            if(val < INT32_MIN || val > INT32_MAX)
            {
                // 溢出 → 退回用 double 表示
                double dval = static_cast<double>(val);
                return Json(dval);
            }

            return Json(static_cast<int32_t>(val));
        }
    }

    // ═══════════════════════════════════════════
    //  parse_string
    //
    //  解析 "..." 包裹的字符串，处理转义序列。
    //  Arena 分配最终 std::string 或 ShortString。
    // ═══════════════════════════════════════════

    Json JsonParser::parse_string()
    {
        assert(peek() == '"');
        advance();   // 跳过开头的 "

        std::string buf;
        buf.reserve(64);   // 减少小字符串重分配

        while(!at_end())
        {
            char c = advance();

            if(c == '"')
            {
                // 字符串结束
                return Json::create_string(buf, m_arena_id);
            }

            if(c == '\\')
            {
                // 转义序列
                if(at_end())
                    error("unexpected end of string after backslash");

                char esc = advance();
                switch(esc)
                {
                case '"':  buf += '"';  break;
                case '\\': buf += '\\'; break;
                case '/':  buf += '/';  break;
                case 'b':  buf += '\b'; break;
                case 'f':  buf += '\f'; break;
                case 'n':  buf += '\n'; break;
                case 'r':  buf += '\r'; break;
                case 't':  buf += '\t'; break;
                case 'u':
                {
                    // \uXXXX — 解析 4 位十六进制 Unicode 码点
                    if(m_pos + 4 > m_src.size())
                        error("expected 4 hex digits after \\u");

                    std::string_view hex = m_src.substr(m_pos, 4);
                    m_pos += 4;

                    char* end = nullptr;
                    unsigned long codepoint = std::strtoul(hex.data(), &end, 16);
                    if(end != hex.data() + 4)
                        error("invalid \\u escape sequence");

                    // 编码为 UTF-8
                    if(codepoint <= 0x7F)
                    {
                        buf += static_cast<char>(codepoint);
                    }
                    else if(codepoint <= 0x7FF)
                    {
                        buf += static_cast<char>(0xC0 | (codepoint >> 6));
                        buf += static_cast<char>(0x80 | (codepoint & 0x3F));
                    }
                    else if(codepoint <= 0xFFFF)
                    {
                        buf += static_cast<char>(0xE0 | (codepoint >> 12));
                        buf += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                        buf += static_cast<char>(0x80 | (codepoint & 0x3F));
                    }
                    break;
                }
                default:
                    error("unknown escape sequence");
                }
            }
            else
            {
                // 控制字符在 JSON 字符串中不合法
                if(static_cast<unsigned char>(c) < 0x20)
                    error("unescaped control character in string");

                buf += c;
            }
        }

        error("unterminated string");
    }

    // ═══════════════════════════════════════════
    //  parse_array
    // ═══════════════════════════════════════════

    Json JsonParser::parse_array()
    {
        assert(peek() == '[');
        advance();   // 跳过 [

        Json arr = Json::create_array(m_arena_id);

        skip_whitespace();

        // 空数组
        if(peek() == ']')
        {
            advance();
            return arr;
        }

        while(true)
        {
            arr.push_back(parse_value());

            skip_whitespace();

            char c = advance();
            if(c == ']')
                break;

            if(c != ',')
                error("expected ',' or ']' in array");

            skip_whitespace();

            // 处理尾部逗号（宽容模式：允许但不强制）
            if(peek() == ']')
            {
                advance();
                break;
            }
        }

        return arr;
    }

    // ═══════════════════════════════════════════
    //  parse_object
    // ═══════════════════════════════════════════

    Json JsonParser::parse_object()
    {
        assert(peek() == '{');
        advance();   // 跳过 {

        Json obj = Json::create_object(m_arena_id);

        skip_whitespace();

        // 空对象
        if(peek() == '}')
        {
            advance();
            return obj;
        }

        while(true)
        {
            skip_whitespace();

            // 解析 key（必须是字符串）
            if(peek() != '"')
                error("expected string key in object");

            Json key_json = parse_string();

            // key 可能是 ShortString（≤7B）或 String（Arena 分配）
            // as_cstr() 两者都支持，构造临时 std::string 用于插入
            std::string key_str(key_json.as_cstr());

            skip_whitespace();

            if(advance() != ':')
                error("expected ':' after object key");

            Json val = parse_value();

            // insert：Arena 分配 key 的副本
            obj.insert(key_str, val);

            skip_whitespace();

            char c = advance();
            if(c == '}')
                break;

            if(c != ',')
                error("expected ',' or '}' in object");

            // 处理尾部逗号
            skip_whitespace();
            if(peek() == '}')
            {
                advance();
                break;
            }
        }

        return obj;
    }

    // ═══════════════════════════════════════════
    //  公开接口
    // ═══════════════════════════════════════════

    namespace JSON
    {
        Json parse(const std::string& json_str, ArenaID arena_id)
        {
            JsonParser parser(std::string_view(json_str), arena_id);
            Json result = parser.parse_value();

            // 确保没有多余的非空白内容
            // （此处不强制要求仅一个值，由调用者决定）

            return result;
        }

        Json parse(const char* json_str, ArenaID arena_id)
        {
            return parse(std::string(json_str), arena_id);
        }
    } // namespace JSON

} // namespace ID
