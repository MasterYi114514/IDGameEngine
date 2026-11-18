#include "project/Cli/PluginSpec.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/**
 *  `--plugin` 三段语法的解析实现（docs/id/10 §10.4）。
 *
 *  只做分段与取值形态判定：插件名与选项名的存在性、选项取值与该 `type` 的合法集由解析阶段判定，
 *  这里报"语法不成立"的三种情形（段数超出、linkage 段非法、选项段缺 `=`）。
 */

namespace ID::Project
{
    namespace
    {
        // 按分隔符切分；保留空段（空段本身就是语法错误，不能被静默丢弃）
        std::vector<std::string_view> split(std::string_view text, char separator)
        {
            std::vector<std::string_view> parts;
            std::size_t                   offset = 0;
            while (true)
            {
                const std::size_t end = text.find(separator, offset);
                if (end == std::string_view::npos)
                {
                    parts.push_back(text.substr(offset));
                    return parts;
                }
                parts.push_back(text.substr(offset, end - offset));
                offset = end + 1;
            }
        }

        // 选项段：逗号分隔的 `NAME=VALUE` 列表；本函数只判形态，不判名字是否存在、取值是否合法
        bool parse_option_list(std::string_view text, std::vector<OptionAssignment>& out, std::string& error)
        {
            const std::vector<std::string_view> assignments = split(text, ',');
            for (const std::string_view assignment : assignments)
            {
                if (assignment.empty())
                {
                    error = "--plugin 的选项列表里有空项";
                    return false;
                }

                const std::size_t equals = assignment.find('=');
                if (equals == std::string_view::npos)
                {
                    error = "--plugin 的选项缺少 '='：" + std::string(assignment);
                    return false;
                }

                const std::string_view name  = assignment.substr(0, equals);
                const std::string_view value = assignment.substr(equals + 1);
                if (name.empty() || value.empty())
                {
                    error = "--plugin 的选项名或取值为空：" + std::string(assignment);
                    return false;
                }

                OptionAssignment option;
                option.name  = std::string(name);
                option.value = std::string(value);
                out.push_back(std::move(option));
            }
            return true;
        }

        PluginSpecParse fail(std::string reason)
        {
            PluginSpecParse result;
            result.is_ok = false;
            result.error = std::move(reason);
            return result;
        }
    } // namespace

    PluginSpecParse parse_plugin_spec(std::string_view text)
    {
        PluginSpecParse result;
        result.spec.raw = std::string(text);

        if (text.empty())
        {
            return fail("--plugin 的取值为空");
        }

        const std::vector<std::string_view> segments = split(text, ':');
        if (segments.size() > 3)
        {
            return fail("--plugin 的段数超出（固定为 <Name>[:static|dynamic][:OPTION=ON,...]）：" + std::string(text));
        }

        if (segments[0].empty())
        {
            return fail("--plugin 缺少插件名：" + std::string(text));
        }
        if (segments[0].find(',') != std::string_view::npos || segments[0].find('=') != std::string_view::npos)
        {
            return fail("--plugin 的插件名里出现 ',' 或 '='（一个参数只描述一个插件）：" + std::string(text));
        }
        result.spec.name = std::string(segments[0]);

        if (segments.size() == 1)
        {
            result.is_ok = true;
            return result;
        }

        std::string error;
        const std::string_view second = segments[1];

        // 两段形态允许直接写选项列表：出现 '=' 即按选项段解释，否则必须是 linkage
        if (second.find('=') != std::string_view::npos)
        {
            if (segments.size() > 2)
            {
                return fail("--plugin 的 linkage 段缺失，但出现了第三段：" + std::string(text));
            }
            if (!parse_option_list(second, result.spec.options, error))
            {
                return fail(error);
            }
            result.is_ok = true;
            return result;
        }

        Linkage linkage = Linkage::Static;
        if (!parse_linkage(second, linkage))
        {
            return fail("--plugin 的 linkage 段只能是 static 或 dynamic：" + std::string(second));
        }
        result.spec.linkage = linkage;

        if (segments.size() == 3)
        {
            if (segments[2].empty())
            {
                return fail("--plugin 的选项列表为空：" + std::string(text));
            }
            if (!parse_option_list(segments[2], result.spec.options, error))
            {
                return fail(error);
            }
        }

        result.is_ok = true;
        return result;
    }
} // namespace ID::Project
