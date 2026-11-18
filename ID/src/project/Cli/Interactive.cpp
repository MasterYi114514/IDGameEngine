#include "project/Cli/Interactive.hpp"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "project/Base/Text.hpp"

#if defined(_WIN32)
    #include <io.h>
    #include <stdio.h>
#else
    #include <unistd.h>
#endif

/**
 *  交互式选择的行式实现（docs/id/10 §10.5）。
 *
 *  界面是行式提示：先一次性列出可发现插件与序号，再读一行序号列表，然后逐插件询问 linkage 与各个选项。
 *  每一行的空输入都表示"取默认值"，因此"按回车走完"与"显式写出全部取值"得到同一个结果。
 */

namespace ID::Project
{
    namespace
    {
        // 读一行并去掉行尾的 CR：Windows 的 getline 只吃 '\n'，'\r' 会留在文本里
        bool read_line(std::string& out)
        {
            if (!std::getline(std::cin, out))
            {
                return false;
            }
            while (!out.empty() && (out.back() == '\r' || out.back() == '\n'))
            {
                out.pop_back();
            }
            return true;
        }

        std::vector<std::string> split_ascii_space(std::string_view text)
        {
            std::vector<std::string> parts;
            std::size_t              offset = 0;
            while (offset < text.size())
            {
                const std::size_t begin = text.find_first_not_of(" \t", offset);
                if (begin == std::string_view::npos)
                {
                    break;
                }
                const std::size_t end = text.find_first_of(" \t", begin);
                parts.emplace_back(text.substr(begin, end == std::string_view::npos ? text.size() - begin
                                                                                   : end - begin));
                offset = end == std::string_view::npos ? text.size() : end;
            }
            return parts;
        }

        // 十进制的序号文本；非数字返回 false
        bool parse_index(std::string_view text, std::size_t& out) noexcept
        {
            if (text.empty())
            {
                return false;
            }

            std::size_t value = 0;
            for (const char unit : text)
            {
                if (unit < '0' || unit > '9')
                {
                    return false;
                }
                value = value * 10 + static_cast<std::size_t>(unit - '0');
            }
            out = value;
            return true;
        }

        InteractiveResult cancel(std::string reason)
        {
            InteractiveResult result;
            result.is_cancelled = true;
            result.reason       = std::move(reason);
            return result;
        }

        InteractiveResult reject(std::string reason)
        {
            InteractiveResult result;
            result.is_ok = false;
            result.reason = std::move(reason);
            return result;
        }

        std::string requirement_text_of(const PluginManifest& manifest)
        {
            const std::string_view text = requirement_text(manifest.requirement);
            return text.empty() ? std::string("皆可") : std::string(text);
        }

        std::string make_dependency_text(const PluginManifest& manifest)
        {
            if (manifest.dependencies.empty())
            {
                return std::string("无");
            }

            std::string text;
            for (const std::string& dependency : manifest.dependencies)
            {
                if (!text.empty())
                {
                    text.append("、");
                }
                text.append(dependency);
            }
            return text;
        }

        // 闭包补齐：选中下游即自动勾选上游（按清单 dependencies 递归），结果按插件名排序
        void add_with_dependencies(const PluginCatalog& catalog, std::string_view name,
                                  std::vector<std::string>& selected)
        {
            if (std::find(selected.begin(), selected.end(), name) != selected.end())
            {
                return;
            }

            const CatalogEntry* entry = find_plugin(catalog, name);
            if (entry == nullptr)
            {
                return;
            }

            selected.emplace_back(name);
            for (const std::string& dependency : entry->manifest.dependencies)
            {
                add_with_dependencies(catalog, dependency, selected);
            }
        }
    } // namespace

    bool is_stdin_terminal() noexcept
    {
    #if defined(_WIN32)
        return ::_isatty(::_fileno(stdin)) != 0;
    #else
        return ::isatty(STDIN_FILENO) != 0;
    #endif
    }

    InteractiveResult run_interactive_selection(const PluginCatalog& catalog)
    {
        if (!is_stdin_terminal())
        {
            InteractiveResult result;
            result.is_ok = false;
            result.reason = "标准输入不是终端（非交互环境），不读取 stdin";
            return result;
        }

        if (catalog.entries.empty())
        {
            InteractiveResult result;
            result.is_ok  = true;
            result.is_empty = true;
            return result;
        }

        std::cout << "可发现的插件（按名字排序）：\n";
        for (std::size_t index = 0; index < catalog.entries.size(); ++index)
        {
            const PluginManifest& manifest = catalog.entries[index].manifest;
            std::cout << "  [" << (index + 1) << "] " << manifest.name << "（清单要求: "
                      << requirement_text_of(manifest) << "；依赖: " << make_dependency_text(manifest) << "）\n";
        }
        std::cout << "请输入要选择的插件序号（空格分隔；输入 0 表示不选任何插件）：" << std::flush;

        std::string line;
        if (!read_line(line))
        {
            return reject("读取标准输入失败");
        }

        const std::vector<std::string> tokens = split_ascii_space(line);
        if (tokens.empty())
        {
            return cancel("未输入任何序号");
        }

        std::vector<std::string> selected;
        for (const std::string& token : tokens)
        {
            std::size_t index = 0;
            if (!parse_index(token, index))
            {
                return reject("序号不是十进制数字：" + token);
            }
            if (index == 0)
            {
                InteractiveResult result;
                result.is_ok     = true;
                result.is_empty  = true;
                return result;
            }
            if (index > catalog.entries.size())
            {
                return reject("序号超出范围：" + token);
            }
            add_with_dependencies(catalog, catalog.entries[index - 1].manifest.name, selected);
        }

        std::sort(selected.begin(), selected.end());

        InteractiveResult result;
        for (const std::string& name : selected)
        {
            const CatalogEntry* entry = find_plugin(catalog, name);
            if (entry == nullptr)
            {
                return reject("选中了未发现的插件：" + name);
            }
            const PluginManifest& manifest = entry->manifest;

            PluginSpec spec;
            spec.name = name;

            const Linkage default_form = default_linkage(manifest.requirement);
            std::cout << "插件 " << name << " 的 linkage（static|dynamic，直接回车 = "
                      << to_string(default_form) << "）：" << std::flush;
            if (!read_line(line))
            {
                return reject("读取标准输入失败");
            }

            if (trim_ascii_space(line).empty())
            {
                spec.linkage = default_form;
            }
            else
            {
                Linkage linkage = Linkage::Static;
                if (!parse_linkage(trim_ascii_space(line), linkage))
                {
                    return reject("linkage 只能是 static 或 dynamic：" + line);
                }
                spec.linkage = linkage;
            }

            // 选项按名字排序后再询问：结果顺序与清单声明顺序无关，便于用 --plugin 复现
            std::vector<const ManifestOption*> options;
            options.reserve(manifest.options.size());
            for (const ManifestOption& option : manifest.options)
            {
                options.push_back(&option);
            }
            std::sort(options.begin(), options.end(),
                      [](const ManifestOption* left, const ManifestOption* right) noexcept
                      {
                          return std::string_view(left->name) < std::string_view(right->name);
                      });

            for (const ManifestOption* option : options)
            {
                std::cout << "选项 " << option->name << "（ON|OFF，直接回车 = "
                          << (option->default_on ? "ON" : "OFF") << "）：" << std::flush;
                if (!read_line(line))
                {
                    return reject("读取标准输入失败");
                }

                OptionAssignment assignment;
                assignment.name = option->name;
                if (trim_ascii_space(line).empty())
                {
                    assignment.value = option->default_on ? "ON" : "OFF";
                }
                else
                {
                    const std::string_view value = trim_ascii_space(line);
                    if (value != "ON" && value != "OFF")
                    {
                        return reject("选项取值只能是 ON 或 OFF：" + line);
                    }
                    assignment.value = std::string(value);
                }
                spec.options.push_back(std::move(assignment));
            }

            result.plugins.push_back(std::move(spec));
        }

        result.is_ok = true;
        return result;
    }
} // namespace ID::Project
