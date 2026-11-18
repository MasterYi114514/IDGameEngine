#include "project/Resolve/EffectiveSelection.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/**
 *  有效选择的展开：直接项取 `plugins.json` 的取值，闭包补齐项取清单缺省值。
 *
 *  展开后把清单声明的每个选项都写出来（含未被用户写出的），因此"工具补写初值"与"用户显式写同值"
 *  等价，不产生假差异。诊断只在这里产出四类：plugin_unknown、requirement_conflict、option_unknown、
 *  option_value_invalid。
 */

namespace ID::Project
{
    namespace
    {
        void sort_options(std::vector<EffectiveOption>& options)
        {
            std::sort(options.begin(), options.end(),
                      [](const EffectiveOption& left, const EffectiveOption& right) noexcept
                      {
                          return std::string_view(left.name) < std::string_view(right.name);
                      });
        }

        void sort_plugins(std::vector<EffectivePlugin>& plugins)
        {
            std::sort(plugins.begin(), plugins.end(),
                      [](const EffectivePlugin& left, const EffectivePlugin& right) noexcept
                      {
                          return std::string_view(left.name) < std::string_view(right.name);
                      });
        }

        // 被忽略/未发现的插件：附 catalog 记下的原始忽略原因
        ProjectDiagnostic make_plugin_unknown(const PluginCatalog& catalog, std::string_view name)
        {
            const UnknownPluginReason reason = explain_unknown(catalog, name);
            std::string actual = reason.is_ignored ? "该插件已被忽略" : "IDPlugin/ 下未被发现";
            if (!reason.original_reason.empty())
            {
                actual += "（原始原因：";
                actual += reason.original_reason;
                actual += "）";
            }

            return ProjectDiagnostic{ProjectCategory::PluginUnknown,
                                     std::string(name),
                                     std::string("插件名对应一个已发现且未被忽略的清单"),
                                     std::move(actual),
                                     std::string("IDPlugin"),
                                     std::string("修正清单或补齐该插件目录后重跑；工具不按目录顺序或版本择一")};
        }
    } // namespace

    EffectivePlugin expand_manifest_defaults(const PluginManifest& manifest, bool is_direct)
    {
        EffectivePlugin plugin;
        plugin.name           = manifest.name;
        plugin.linkage        = default_linkage(manifest.requirement);
        plugin.linkage_source = LinkageSource::Requirement;
        plugin.is_direct      = is_direct;

        plugin.options.reserve(manifest.options.size());
        for (const ManifestOption& option : manifest.options)
        {
            EffectiveOption effective;
            effective.name        = option.name;
            effective.value       = option.default_on;
            effective.source      = OptionSource::Default;
            effective.description = option.description;
            plugin.options.push_back(std::move(effective));
        }
        sort_options(plugin.options);
        return plugin;
    }

    EffectiveSelection expand_direct_selection(const SelectionFile& selection, const PluginCatalog& catalog)
    {
        EffectiveSelection result;
        result.project_name = selection.name;

        for (const SelectionPlugin& chosen : selection.plugins)
        {
            const CatalogEntry* entry = find_plugin(catalog, chosen.name);
            if (entry == nullptr)
            {
                result.diagnostics.add(make_plugin_unknown(catalog, chosen.name));
                continue;
            }

            const PluginManifest& manifest = entry->manifest;
            EffectivePlugin plugin = expand_manifest_defaults(manifest, true);
            plugin.linkage         = chosen.linkage;
            plugin.linkage_source  = LinkageSource::Explicit;

            // 直接选择与作者约束冲突：报 requirement_conflict，不改写用户选择
            if ((manifest.requirement == Requirement::StaticOnly && chosen.linkage == Linkage::Dynamic)
                || (manifest.requirement == Requirement::SharedOnly && chosen.linkage == Linkage::Static))
            {
                result.diagnostics.add(ProjectDiagnostic{
                    ProjectCategory::RequirementConflict,
                    chosen.name,
                    "linkage_type 与清单 requirements 相容",
                    "选择 " + std::string(to_string(chosen.linkage)) + "，清单要求 "
                        + std::string(requirement_text(manifest.requirement)),
                    manifest.source_root + "/plugin_config.json",
                    std::string("把 plugins.json 的 linkage_type 改成清单允许的形态，或由插件作者调整 requirements")});
            }

            for (const OptionAssignment& assignment : chosen.options)
            {
                bool is_declared = false;
                for (EffectiveOption& option : plugin.options)
                {
                    if (option.name != assignment.name)
                    {
                        continue;
                    }

                    is_declared = true;
                    if (assignment.value != "ON" && assignment.value != "OFF")
                    {
                        result.diagnostics.add(ProjectDiagnostic{
                            ProjectCategory::OptionValueInvalid,
                            chosen.name + "." + assignment.name,
                            "取值 \"ON\" 或 \"OFF\"（大小写敏感）",
                            assignment.value,
                            manifest.source_root + "/plugin_config.json",
                            std::string("按清单选项的 type 给合法取值后重跑")});
                        break;
                    }
                    option.value  = assignment.value == "ON";
                    option.source = OptionSource::Explicit;
                    break;
                }

                if (!is_declared)
                {
                    result.diagnostics.add(ProjectDiagnostic{
                        ProjectCategory::OptionUnknown,
                        chosen.name + "." + assignment.name,
                        "选项名在清单 options 中已声明",
                        "清单未声明该选项",
                        manifest.source_root + "/plugin_config.json",
                        std::string("删除该选项赋值，或由插件作者在清单里声明该选项")});
                }
            }

            result.direct.push_back(std::move(plugin));
        }

        sort_plugins(result.direct);
        // 闭包尚未补齐：resolve_closure 在补上依赖项后填满 all
        result.all      = result.direct;
        result.is_valid = !result.diagnostics.has_error();
        return result;
    }

    bool find_option_value(const EffectivePlugin& plugin, std::string_view option_name, bool& out) noexcept
    {
        for (const EffectiveOption& option : plugin.options)
        {
            if (std::string_view(option.name) == option_name)
            {
                out = option.value;
                return true;
            }
        }
        return false;
    }
} // namespace ID::Project
