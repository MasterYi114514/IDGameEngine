#include "JsonWriter.hpp"
#include "Arena.hpp"
#include "ArenaAllocator.hpp"
#include "Log.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>

namespace ID::JSON
{
    using ArrayImpl  = std::vector<Json, ArenaAllocator<Json>>;
    using ObjectImpl = std::unordered_map<
        std::string,
        Json,
        std::hash<std::string>,
        std::equal_to<>,
        ArenaAllocator<std::pair<const std::string, Json>>
    >;

    namespace
    {
        const char* indent_string(IndentStyle indent)
        {
            switch (indent)
            {
                case IndentStyle::Compact:   return "";
                case IndentStyle::TwoSpace:  return "  ";
                case IndentStyle::FourSpace: return "    ";
                case IndentStyle::Tab:       return "\t";
                default:                     return "    ";
            }
        }

        void write_string(std::ostream& os, const char* str)
        {
            os << '"';

            for (const char* p = str; *p != '\0'; ++p)
            {
                unsigned char c = static_cast<unsigned char>(*p);

                switch (c)
                {
                    case '"':  os << "\\\""; break;
                    case '\\': os << "\\\\"; break;
                    case '\b': os << "\\b";  break;
                    case '\f': os << "\\f";  break;
                    case '\n': os << "\\n";  break;
                    case '\r': os << "\\r";  break;
                    case '\t': os << "\\t";  break;
                    default:
                        if (c < 0x20)
                        {
                            // 控制字符 → \u00XX
                            char buf[8];
                            std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                            os << buf;
                        }
                        else
                        {
                            os << c;
                        }
                        break;
                }
            }

            os << '"';
        }

        void write_value(std::ostream& os, const Json& value, int depth, const std::string& indent_str)
        {
            const bool compact = indent_str.empty();

            switch (value.type())
            {
                case JSON::Type::Null:
                    os << "null";
                    break;

                case JSON::Type::Bool:
                    os << (value.as_bool() ? "true" : "false");
                    break;

                case JSON::Type::Int:
                    os << value.as_int();
                    break;

                case JSON::Type::Float:
                {
                    // 保证 round-trip 精度的浮点输出
                    // %.17g 会把 1.0 输出成 "1"（无小数点），JsonParser 解析回 Int 类型，
                    // 导致反序列化 as_float() 断言崩溃 → 必须保留小数点或指数标记
                    char buf[32];
                    std::snprintf(buf, sizeof(buf), "%.17g", value.as_float());

                    std::string text(buf);
                    if (text.find_first_of(".eEnNiI") == std::string::npos)
                    {
                        os << text << ".0";
                    }
                    else
                    {
                        os << text;
                    }
                    break;
                }

                case JSON::Type::ShortString:
                case JSON::Type::String:
                    write_string(os, value.as_cstr());
                    break;

                case JSON::Type::Array:
                {
                    const auto& arr = *static_cast<const ArrayImpl*>(value.get_ptr());
                    if (arr.empty())
                    {
                        os << "[]";
                        break;
                    }

                    os << "[";
                    if (!compact) os << '\n';

                    for (size_t i = 0; i < arr.size(); ++i)
                    {
                        if (!compact)
                        {
                            for (int d = 0; d <= depth; ++d) os << indent_str;
                        }

                        write_value(os, arr[i], depth + 1, indent_str);

                        if (i + 1 < arr.size())
                            os << ",";
                        if (!compact)
                            os << '\n';
                    }

                    if (!compact)
                    {
                        for (int d = 0; d < depth; ++d) os << indent_str;
                    }
                    os << "]";
                    break;
                }

                case JSON::Type::Object:
                {
                    const auto& obj = *static_cast<const ObjectImpl*>(value.get_ptr());
                    if (obj.empty())
                    {
                        os << "{}";
                        break;
                    }

                    os << "{";
                    if (!compact) os << '\n';

                    size_t count = 0;
                    for (const auto& pair : obj)
                    {
                        if (!compact)
                        {
                            for (int d = 0; d <= depth; ++d) os << indent_str;
                        }

                        write_string(os, pair.first.c_str());
                        os << ": ";
                        write_value(os, pair.second, depth + 1, indent_str);

                        if (++count < obj.size())
                            os << ",";
                        if (!compact)
                            os << '\n';
                    }

                    if (!compact)
                    {
                        for (int d = 0; d < depth; ++d) os << indent_str;
                    }
                    os << "}";
                    break;
                }
            }
        }
    } // 匿名命名空间

    std::string to_string(const Json& value, IndentStyle indent)
    {
        std::ostringstream oss;
        write_value(oss, value, 0, indent_string(indent));
        return oss.str();
    }

    void write_to_file(const std::string& path, const Json& json, IndentStyle indent)
    {
        std::ofstream ofs(path);
        if (!ofs)
        {
            IDJSON_ERROR("[JsonWriter] 无法打开文件: {}", path);
            return;
        }
        write_value(ofs, json, 0, indent_string(indent));
    }

    void write(std::ostream& os, const Json& value, IndentStyle indent)
    {
        write_value(os, value, 0, indent_string(indent));

        if(indent != IndentStyle::Compact)
            os << '\n';
    }
} // namespace ID