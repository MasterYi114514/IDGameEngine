#include "project/Input/PluginCatalog.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "project/Base/FileIo.hpp"
#include "project/Base/PathText.hpp"

/**
 *  插件发现的两遍扫描与忽略判定。
 *
 *  第一遍只解析清单（失败即忽略并记 warn），第二遍在全部清单就位后检查依赖引用完整性（引用未发现
 *  插件的插件整体忽略）。两遍都不读资产内容、不改任何文件。
 */

namespace ID::Project
{
    namespace
    {
        constexpr std::string_view kManifestFileName = "plugin_config.json";
        constexpr std::string_view kCommonDirectory  = "Common";
        constexpr std::string_view kVendorDirectory  = "Vendor";

        bool is_skipped_directory(std::string_view name) noexcept
        {
            return name == kCommonDirectory || name == kVendorDirectory;
        }

        void sort_entries(std::vector<CatalogEntry>& entries)
        {
            std::sort(entries.begin(), entries.end(),
                      [](const CatalogEntry& left, const CatalogEntry& right) noexcept
                      {
                          return std::string_view(left.manifest.name) < std::string_view(right.manifest.name);
                      });
        }

        void sort_names(std::vector<std::string>& names)
        {
            std::sort(names.begin(), names.end(),
                      [](const std::string& left, const std::string& right) noexcept
                      {
                          return std::string_view(left) < std::string_view(right);
                      });
        }
    } // namespace

    PluginCatalog discover_plugins(const std::filesystem::path& id_plugin_root)
    {
        PluginCatalog catalog;

        const DirectoryList listing = list_directory(id_plugin_root);
        if (!listing.is_ok)
        {
            catalog.is_ok = false;
            return catalog;
        }
        catalog.is_ok = true;

        std::vector<CatalogEntry> entries;
        std::vector<std::string>  ignored;

        // 第一遍：逐个一级子目录解析清单
        for (const std::string& name : listing.names)
        {
            if (is_skipped_directory(name))
            {
                continue;
            }

            const std::filesystem::path plugin_directory = id_plugin_root / to_native(name);
            bool is_directory_ok = false;
            if (!is_directory(plugin_directory, is_directory_ok))
            {
                continue;   // 一级条目不是目录：不参与发现
            }

            const std::filesystem::path manifest_path = plugin_directory / to_native(kManifestFileName);
            const FileRead read = read_file(manifest_path);
            if (read.status == ReadStatus::NotFound)
            {
                continue;   // 目录内没有清单：不参与发现，也不报错
            }
            if (read.status == ReadStatus::Failed)
            {
                catalog.diagnostics.add(ProjectDiagnostic{ProjectCategory::PluginConfigInvalid,
                                                          name,
                                                          std::string("可读的 plugin_config.json"),
                                                          std::string("读取失败"),
                                                          std::string(kManifestFileName),
                                                          std::string("检查文件权限后重跑；该插件在可读前被忽略")});
                ignored.push_back(name);
                continue;
            }

            ManifestParse parsed = parse_manifest(read.bytes, name);
            if (!parsed.is_ok)
            {
                if (parsed.diagnostic.has_value())
                {
                    ProjectDiagnostic diagnostic = std::move(*parsed.diagnostic);
                    // 诊断对象换成插件名，便于后续 plugin_unknown 附原始原因
                    diagnostic.subject = name;
                    catalog.diagnostics.add(std::move(diagnostic));
                }
                ignored.push_back(name);
                continue;
            }

            CatalogEntry entry;
            entry.manifest = std::move(parsed.manifest);
            entry.bytes    = static_cast<std::uint64_t>(read.bytes.size());
            entries.push_back(std::move(entry));
        }

        sort_entries(entries);

        // 第二遍：依赖引用检查（全部清单解析完成后）；引用未发现插件的插件整体忽略。
        // 名字集合先独立取好：下面会把条目移入 kept，移动后的条目不再参与引用判定
        std::vector<std::string> discovered_names;
        discovered_names.reserve(entries.size());
        for (const CatalogEntry& entry : entries)
        {
            discovered_names.push_back(entry.manifest.name);
        }

        std::vector<CatalogEntry> kept;
        kept.reserve(entries.size());
        for (CatalogEntry& entry : entries)
        {
            std::vector<std::string> missing;
            for (const std::string& dependency : entry.manifest.dependencies)
            {
                const bool is_discovered =
                    std::find(discovered_names.begin(), discovered_names.end(), dependency) != discovered_names.end();
                if (!is_discovered)
                {
                    missing.push_back(dependency);
                }
            }

            if (missing.empty())
            {
                kept.push_back(std::move(entry));
                continue;
            }

            for (const std::string& dependency : missing)
            {
                catalog.diagnostics.add(ProjectDiagnostic{
                    ProjectCategory::DependencyUnknown,
                    entry.manifest.name,
                    "dependencies 引用的插件在 IDPlugin/ 下被发现",
                    "清单引用 " + dependency + "，但 IDPlugin/" + dependency
                        + " 未被发现（目录不存在或无合法清单）",
                    entry.manifest.source_root,
                    std::string("补齐该依赖插件或修正清单的 dependencies；该插件在修正前被忽略")});
            }
            ignored.push_back(entry.manifest.name);
        }

        sort_entries(kept);
        sort_names(ignored);

        catalog.entries = std::move(kept);
        catalog.ignored = std::move(ignored);
        return catalog;
    }

    const CatalogEntry* find_plugin(const PluginCatalog& catalog, std::string_view name) noexcept
    {
        for (const CatalogEntry& entry : catalog.entries)
        {
            if (std::string_view(entry.manifest.name) == name)
            {
                return &entry;
            }
        }
        return nullptr;
    }

    UnknownPluginReason explain_unknown(const PluginCatalog& catalog, std::string_view name)
    {
        UnknownPluginReason reason;
        for (const std::string& ignored : catalog.ignored)
        {
            if (std::string_view(ignored) != name)
            {
                continue;
            }

            reason.is_ignored = true;
            // 原始原因取该插件名对应的第一条 warn 诊断的实际文本
            for (std::size_t index = 0; index < catalog.diagnostics.size(); ++index)
            {
                const ProjectDiagnostic& diagnostic = catalog.diagnostics.at(index);
                if (std::string_view(diagnostic.subject) == name)
                {
                    reason.original_reason = diagnostic.actual;
                    break;
                }
            }
            break;
        }
        return reason;
    }
} // namespace ID::Project
