#include "project/Resolve/Naming.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "project/Base/Text.hpp"

/**
 *  命名推导与全局符号冲突预检。
 *
 *  预检只覆盖生成模型已知的名字（项目名、插件目标、汇总目标、vendor 目标、选项变量、选项宏、形态宏与
 *  生成器自带符号）；用户自己 CMakeLists.txt 里动态定义的目标看不到，那由生成侧的 TARGET 守卫在
 *  configure 期拒绝。工具不 sanitize 任何名字。
 */

namespace ID::Project
{
    namespace
    {
        constexpr std::string_view kCommonTarget = "IDCommonHeaders";

        constexpr std::string_view kReservedTargets[] = {
            "all", "ALL_BUILD", "clean", "help", "install", "package",
            "package_source", "test", "RUN_TESTS", "rebuild_cache", "edit_cache", "ZERO_CHECK",
        };

        constexpr std::string_view kReservedGeneratedSymbols[] = {
            "ID_DEBUG", "ID_RELEASE", "ID_OUTPUT_ROOT", "ID_BUILD_TOOLCHAIN", "ID_BUILD_ARCH",
        };

        // 唯一性登记：符号 → 首个产生它的对象，用于在冲突里写出两侧来源
        class SymbolRegistry
        {
        public:
            void record(std::string_view symbol, std::string_view owner)
            {
                m_entries.emplace_back(std::string(symbol), std::string(owner));
            }

            const std::string* find_owner(std::string_view symbol) const noexcept
            {
                for (const auto& entry : m_entries)
                {
                    if (std::string_view(entry.first) == symbol)
                    {
                        return &entry.second;
                    }
                }
                return nullptr;
            }

        private:
            std::vector<std::pair<std::string, std::string>> m_entries = {};
        };

        const ManifestOption* find_manifest_option(const PluginManifest& manifest, std::string_view name) noexcept
        {
            for (const ManifestOption& option : manifest.options)
            {
                if (std::string_view(option.name) == name)
                {
                    return &option;
                }
            }
            return nullptr;
        }

        void sort_names(std::vector<std::string>& names)
        {
            std::sort(names.begin(), names.end(),
                      [](const std::string& left, const std::string& right) noexcept
                      {
                          return std::string_view(left) < std::string_view(right);
                      });
        }

        void sort_plugins(std::vector<PluginNaming>& plugins)
        {
            std::sort(plugins.begin(), plugins.end(),
                      [](const PluginNaming& left, const PluginNaming& right) noexcept
                      {
                          return std::string_view(left.plugin) < std::string_view(right.plugin);
                      });
        }

        // 一个插件由 TAG 派生出的符号清单，供 TAG 折叠碰撞的诊断逐项列出：
        // 选项变量 `ID<TAG>_<OPTION>` 与两个形态宏 `ID_<TAG>_STATIC` / `ID_<TAG>_EXPORTS`
        std::string describe_tag_symbols(const PluginNaming& naming)
        {
            std::string text = naming.plugin;
            text += "：选项变量 ";
            if (naming.option_vars.empty())
            {
                text += "（该插件未声明选项）";
            }
            else
            {
                for (std::size_t index = 0; index < naming.option_vars.size(); ++index)
                {
                    if (index != 0)
                    {
                        text += "、";
                    }
                    text += naming.option_vars[index];
                }
            }
            text += "；形态宏 ";
            text += naming.static_macro;
            text += " / ";
            text += naming.export_macro;
            return text;
        }
    } // namespace

    std::string make_tag(std::string_view plugin_name)
    {
        constexpr std::string_view kPluginPrefix = "ID";
        if (starts_with(plugin_name, kPluginPrefix))
        {
            return ascii_upper(plugin_name.substr(kPluginPrefix.size()));
        }
        return ascii_upper(plugin_name);
    }

    bool check_identifier(std::string_view text) noexcept
    {
        if (text.empty())
        {
            return false;
        }

        const char first = text.front();
        const bool is_alpha = (first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z');
        if (!is_alpha && first != '_')
        {
            return false;
        }

        for (const char raw : text)
        {
            const bool is_upper = raw >= 'A' && raw <= 'Z';
            const bool is_lower = raw >= 'a' && raw <= 'z';
            const bool is_digit = raw >= '0' && raw <= '9';
            if (!is_upper && !is_lower && !is_digit && raw != '_')
            {
                return false;
            }
        }
        return true;
    }

    bool check_project_name(std::string_view text) noexcept
    {
        if (text.empty())
        {
            return false;
        }

        const char first = text.front();
        const bool is_alpha = (first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z');
        if (!is_alpha)
        {
            return false;
        }

        for (const char raw : text)
        {
            const bool is_upper = raw >= 'A' && raw <= 'Z';
            const bool is_lower = raw >= 'a' && raw <= 'z';
            const bool is_digit = raw >= '0' && raw <= '9';
            if (!is_upper && !is_lower && !is_digit && raw != '_' && raw != '-')
            {
                return false;
            }
        }
        return true;
    }

    bool is_reserved_target(std::string_view text) noexcept
    {
        for (const std::string_view reserved : kReservedTargets)
        {
            if (ascii_iequals(text, reserved))
            {
                return true;
            }
        }
        return false;
    }

    bool is_reserved_generated_symbol(std::string_view text) noexcept
    {
        for (const std::string_view reserved : kReservedGeneratedSymbols)
        {
            if (ascii_iequals(text, reserved))
            {
                return true;
            }
        }
        return false;
    }

    NamingPlan plan_naming(std::string_view project_name, const Resolution& resolution,
                           const PluginCatalog& catalog)
    {
        NamingPlan plan;
        plan.common_target       = std::string(kCommonTarget);
        plan.project.plugin      = std::string(project_name);
        plan.project.tag         = {};                  // 项目段没有 TAG
        plan.project.target      = std::string(project_name);

        auto add_conflict = [&plan](std::string subject, std::string expected, std::string actual,
                                    std::string location)
        {
            plan.diagnostics.add(ProjectDiagnostic{
                ProjectCategory::CmakeNameConflict,
                std::move(subject),
                std::move(expected),
                std::move(actual),
                std::move(location),
                std::string("v1 消费模型限制：插件名与 options[].name 的取值必须能映射成合法的 CMake "
                            "标识符，由插件作者改清单解决；项目名/vendor 目标冲突改对应一侧后重跑")});
        };

        // 项目名：字符规则、CMake 内建目标、生成器自带符号、汇总目标同名
        if (!check_project_name(project_name))
        {
            add_conflict(plan.project.target, "项目名匹配 ^[A-Za-z][A-Za-z0-9_-]*$",
                         "不符合项目名字符规则", std::string("ID/project.json"));
        }
        if (is_reserved_target(project_name))
        {
            add_conflict(plan.project.target, "项目名不命中 CMake 内建目标名", "命中保留目标名",
                         std::string("ID/project.json"));
        }
        if (is_reserved_generated_symbol(project_name))
        {
            add_conflict(plan.project.target, "项目名不命中生成器自带宏/变量", "命中保留符号",
                         std::string("ID/project.json"));
        }
        if (ascii_iequals(project_name, kCommonTarget))
        {
            add_conflict(plan.project.target, "项目名与汇总目标不同名", "等于汇总目标 " + std::string(kCommonTarget),
                         std::string("ID/project.json"));
        }

        // 逐插件派生名
        SymbolRegistry targets;
        SymbolRegistry option_vars;
        SymbolRegistry option_defines;
        SymbolRegistry form_macros;

        std::vector<std::pair<std::string, std::string>> tags;      // TAG → 首个插件名

        plan.plugins.reserve(resolution.selection.all.size());
        for (const EffectivePlugin& plugin : resolution.selection.all)
        {
            PluginNaming naming;
            naming.plugin = plugin.name;
            naming.tag    = make_tag(plugin.name);
            naming.target = plugin.name;

            const CatalogEntry* entry = find_plugin(catalog, plugin.name);

            // 三个选项列表按同一份选项名序列逐个填：同长度、同序（选项名 UTF-8 字节序），
            // 未声明 defines 的选项在该位留空串，生成侧按名字对齐取值、不按下标配对已筛选过的列表
            naming.option_vars.reserve(plugin.options.size());
            naming.option_defines.reserve(plugin.options.size());
            naming.option_descriptions.reserve(plugin.options.size());
            for (const EffectiveOption& option : plugin.options)
            {
                const ManifestOption* declared =
                    entry != nullptr ? find_manifest_option(entry->manifest, option.name) : nullptr;

                naming.option_vars.push_back("ID" + naming.tag + "_" + option.name);
                naming.option_defines.push_back(declared != nullptr && declared->define.has_value() ? *declared->define
                                                                                                    : std::string());
                naming.option_descriptions.push_back(declared != nullptr ? declared->description : std::string());
            }

            naming.static_macro = "ID_" + naming.tag + "_STATIC";
            naming.export_macro = "ID_" + naming.tag + "_EXPORTS";

            // 目标名
            if (!check_project_name(naming.target))
            {
                add_conflict(naming.target, "插件目标匹配 ^[A-Za-z][A-Za-z0-9_-]*$", "不符合目标名字符规则",
                             entry != nullptr ? entry->manifest.source_root : std::string("IDPlugin"));
            }
            if (is_reserved_target(naming.target))
            {
                add_conflict(naming.target, "插件目标不命中 CMake 内建目标名", "命中保留目标名",
                             entry != nullptr ? entry->manifest.source_root : std::string("IDPlugin"));
            }
            if (is_reserved_generated_symbol(naming.target))
            {
                add_conflict(naming.target, "插件目标不命中生成器自带宏/变量", "命中保留符号",
                             entry != nullptr ? entry->manifest.source_root : std::string("IDPlugin"));
            }
            if (naming.target == project_name)
            {
                add_conflict(naming.target, "插件目标与项目名不同名", "与项目名相同",
                             entry != nullptr ? entry->manifest.source_root : std::string("IDPlugin"));
            }
            if (ascii_iequals(naming.target, kCommonTarget))
            {
                add_conflict(naming.target, "插件目标与汇总目标不同名",
                             "等于汇总目标 " + std::string(kCommonTarget),
                             entry != nullptr ? entry->manifest.source_root : std::string("IDPlugin"));
            }
            if (const std::string* owner = targets.find_owner(naming.target))
            {
                add_conflict(naming.target, "插件目标全局唯一", "已被 " + *owner + " 占用",
                             entry != nullptr ? entry->manifest.source_root : std::string("IDPlugin"));
            }
            targets.record(naming.target, naming.plugin);

            // 选项变量：唯一 + 标识符合法
            for (const std::string& variable : naming.option_vars)
            {
                if (!check_identifier(variable))
                {
                    add_conflict(variable, "选项变量匹配 ^[A-Za-z_][A-Za-z0-9_]*$（TAG 折叠后仍需合法）",
                                 "TAG \"" + naming.tag + "\" 折叠出非法标识符",
                                 entry != nullptr ? entry->manifest.source_root
                                                  : std::string("IDPlugin"));
                }
                if (const std::string* owner = option_vars.find_owner(variable))
                {
                    add_conflict(variable, "选项变量全局唯一", "已被 " + *owner + " 产生",
                                 entry != nullptr ? entry->manifest.source_root
                                                  : std::string("IDPlugin"));
                }
                option_vars.record(variable, naming.plugin);
            }

            // 选项宏：唯一 + 标识符合法 + 不占用生成器自带符号
            // 空串是"该选项未声明 defines"的占位，只跳过它自己；已声明的宏仍逐个参与下面三项检查
            for (const std::string& define : naming.option_defines)
            {
                if (define.empty())
                {
                    continue;
                }
                if (!check_identifier(define))
                {
                    add_conflict(define, "选项宏匹配 ^[A-Za-z_][A-Za-z0-9_]*$", "非法标识符",
                                 entry != nullptr ? entry->manifest.source_root
                                                  : std::string("IDPlugin"));
                }
                if (is_reserved_generated_symbol(define))
                {
                    add_conflict(define, "选项宏不占用生成器自带宏", "命中保留符号",
                                 entry != nullptr ? entry->manifest.source_root
                                                  : std::string("IDPlugin"));
                }
                if (const std::string* owner = option_defines.find_owner(define))
                {
                    add_conflict(define, "选项宏全局唯一（不按语义判断是否等价）",
                                 "已被 " + *owner + " 声明",
                                 entry != nullptr ? entry->manifest.source_root
                                                  : std::string("IDPlugin"));
                }
                option_defines.record(define, naming.plugin);
            }

            // 形态宏：唯一 + 标识符合法 + 不占用生成器自带符号
            for (const std::string* macro_name : {&naming.static_macro, &naming.export_macro})
            {
                if (!check_identifier(*macro_name))
                {
                    add_conflict(*macro_name, "形态宏匹配 ^[A-Za-z_][A-Za-z0-9_]*$（TAG 折叠后仍需合法）",
                                 "TAG \"" + naming.tag + "\" 折叠出非法标识符",
                                 entry != nullptr ? entry->manifest.source_root
                                                  : std::string("IDPlugin"));
                }
                if (is_reserved_generated_symbol(*macro_name))
                {
                    add_conflict(*macro_name, "形态宏不占用生成器自带宏", "命中保留符号",
                                 entry != nullptr ? entry->manifest.source_root
                                                  : std::string("IDPlugin"));
                }
                if (const std::string* owner = form_macros.find_owner(*macro_name))
                {
                    add_conflict(*macro_name, "形态宏全局唯一", "已被 " + *owner + " 产生",
                                 entry != nullptr ? entry->manifest.source_root
                                                  : std::string("IDPlugin"));
                }
                form_macros.record(*macro_name, naming.plugin);
            }

            // TAG 折叠碰撞登记：同一 TAG 派生出同一组选项变量与两个形态宏，诊断因此必须列出这些具体符号，
            // 用户才能判断要改哪一侧的插件名（spec §2.4、§10.4）
            bool is_tag_known = false;
            for (const std::pair<std::string, std::string>& known : tags)
            {
                if (known.first == naming.tag)
                {
                    is_tag_known = true;

                    const PluginNaming* first = nullptr;
                    for (const PluginNaming& planned : plan.plugins)
                    {
                        if (planned.plugin == known.second)
                        {
                            first = &planned;
                            break;
                        }
                    }

                    std::string actual = "TAG \"" + naming.tag + "\" 同时由 " + known.second + " 与 " + naming.plugin
                                       + " 折叠得到；同一 TAG 派生的符号落在同一命名空间：";
                    if (first != nullptr)
                    {
                        actual += describe_tag_symbols(*first);
                        actual += "；";
                    }
                    actual += describe_tag_symbols(naming);

                    add_conflict(naming.tag,
                                 "两个插件折叠出不同的 TAG（选项变量 ID<TAG>_<OPTION> 与形态宏 "
                                 "ID_<TAG>_STATIC / ID_<TAG>_EXPORTS 因此全局唯一）",
                                 std::move(actual),
                                 entry != nullptr ? entry->manifest.source_root : std::string("IDPlugin"));
                    break;
                }
            }
            if (!is_tag_known)
            {
                tags.emplace_back(naming.tag, naming.plugin);
            }

            plan.plugins.push_back(std::move(naming));
        }
        sort_plugins(plan.plugins);

        // 共享 vendor 目标：唯一、字符规则、保留名（CMake 内建目标与生成器自带符号）、
        // 不与插件目标/项目/汇总目标同名
        std::vector<std::string> vendor_targets;
        for (const EffectivePlugin& plugin : resolution.selection.all)
        {
            const CatalogEntry* entry = find_plugin(catalog, plugin.name);
            if (entry == nullptr)
            {
                continue;
            }
            for (const SharedVendor& vendor : entry->manifest.shared_vendors)
            {
                if (vendor.target.has_value() && !vendor.target->empty())
                {
                    vendor_targets.push_back(*vendor.target);
                }
            }
        }
        sort_names(vendor_targets);
        vendor_targets.erase(std::unique(vendor_targets.begin(), vendor_targets.end()), vendor_targets.end());
        plan.vendor_targets = vendor_targets;

        SymbolRegistry vendor_registry;
        for (const std::string& vendor_target : plan.vendor_targets)
        {
            if (!check_project_name(vendor_target))
            {
                add_conflict(vendor_target, "vendor 目标匹配 ^[A-Za-z][A-Za-z0-9_-]*$", "不符合目标名字符规则",
                             std::string("IDPlugin/Vendor"));
            }
            if (is_reserved_target(vendor_target))
            {
                add_conflict(vendor_target, "vendor 目标不命中 CMake 内建目标名", "命中保留目标名",
                             std::string("IDPlugin/Vendor"));
            }
            if (is_reserved_generated_symbol(vendor_target))
            {
                add_conflict(vendor_target, "vendor 目标不命中生成器自带宏/变量", "命中保留符号",
                             std::string("IDPlugin/Vendor"));
            }
            if (vendor_target == project_name)
            {
                add_conflict(vendor_target, "vendor 目标与项目名不同名", "与项目名相同",
                             std::string("IDPlugin/Vendor"));
            }
            if (ascii_iequals(vendor_target, kCommonTarget))
            {
                add_conflict(vendor_target, "vendor 目标与汇总目标不同名",
                             "等于汇总目标 " + std::string(kCommonTarget), std::string("IDPlugin/Vendor"));
            }
            if (const std::string* owner = targets.find_owner(vendor_target))
            {
                add_conflict(vendor_target, "vendor 目标不与插件目标同名", "与插件目标 " + *owner + " 同名",
                             std::string("IDPlugin/Vendor"));
            }
            if (const std::string* owner = vendor_registry.find_owner(vendor_target))
            {
                add_conflict(vendor_target, "vendor 目标全局唯一", "已被 " + *owner + " 占用",
                             std::string("IDPlugin/Vendor"));
            }
            vendor_registry.record(vendor_target, std::string("shared_vendors"));
        }

        plan.is_valid = !plan.diagnostics.has_error();
        return plan;
    }
} // namespace ID::Project
