#include "project/Resolve/AssetPlan.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "project/Base/FileIo.hpp"
#include "project/Base/PathText.hpp"
#include "project/Base/Text.hpp"

/**
 *  资产计划的逐文件枚举与完整性检查。
 *
 *  复制目标与源路径在项目内同形（插件资产复制到 `IDPlugin/<Plugin>/...`、Common 头复制到
 *  `IDPlugin/Common/...`、共享 vendor 复制到 `IDPlugin/Vendor/...`），因此目标相对路径同时就是源相对
 *  工程根的路径；生成项的来源为空。
 *
 *  每个目标与来源路径都经 `normalize_relative` 落到项目内形态后才登记（`Base/PathText.hpp` 的唯一入口）：
 *  绝对路径、盘符、`..`、非 POSIX 形态与 Windows 保留名一律按 `path_outside_project`（码 1）报告，
 *  且该条目不进入文件集合与目录集合——计划里不会出现未经校验的复制目标。
 */

namespace ID::Project
{
    namespace
    {
        constexpr std::string_view kPluginRoot      = "IDPlugin";
        constexpr std::string_view kCommonRoot      = "IDPlugin/Common";
        constexpr std::string_view kVendorRoot      = "IDPlugin/Vendor";
        constexpr std::string_view kManifestFile    = "plugin_config.json";
        constexpr std::string_view kReadmeFile      = "README.md";
        constexpr std::string_view kCMakeFileName   = "CMakeLists.txt";

        bool digest_equals(const Digest& left, const Digest& right)
        {
            return left.to_hex() == right.to_hex();
        }

        // 收集器：枚举过程中累积待写入集合，finish() 统一排序、去重并转成资产计划
        class PlanCollector
        {
        public:
            PlanCollector(const std::filesystem::path& engine_root, const std::vector<std::string>& plugin_names,
                          DiagnosticSet& diagnostics)
                : m_engine_root(engine_root)
                , m_plugin_names(plugin_names)
                , m_diagnostics(diagnostics)
            {}

            /**
             *  @brief 把一条资产路径规范化到项目内形态
             *  @param text 原始路径文本（目标或来源，只读）
             *  @param subject 诊断对象（插件名、清单条目或该路径本身）
             *  @param location 诊断定位（条目身份）
             *  @return 通过校验时返回规范化文本；否则报 `path_outside_project`（码 1）并返回空
             *  @note 越界与形态违规统一映射为同一类别（spec §2.3），`PathVerdict` 只进 actual 文案
             */
            std::optional<std::string> normalize_path(std::string_view text, std::string_view subject,
                                                      std::string_view location)
            {
                PathCheck check = normalize_relative(text);
                if (check.verdict == PathVerdict::Ok)
                {
                    // 形态合法后再判重解析点：链接（junction / 符号链接）指向项目外即越界，
                    // 只有"形态合法但不存在或类型不符"才按 asset_missing 报告（spec §2.3、§10.6）
                    const std::filesystem::path absolute = m_engine_root / to_native(check.text);
                    const PathCheck             link     = check_reparse_points_under(absolute, m_engine_root);
                    if (link.verdict == PathVerdict::Ok)
                    {
                        return std::move(check.text);
                    }

                    std::string link_actual = "源路径命中重解析点或属性查询失败：";
                    link_actual += link.detail.empty() ? to_utf8(absolute) : link.detail;
                    m_diagnostics.add(ProjectDiagnostic{ProjectCategory::PathOutsideProject,
                                                        std::string(subject),
                                                        std::string("源路径的祖先链不含符号链接、junction 或挂载点"),
                                                        std::move(link_actual),
                                                        std::string(location),
                                                        std::string("把该路径上的链接换成工程内的真实目录后重跑"
                                                                    "（spec §2.3）")});
                    return std::nullopt;
                }

                std::string actual = "非法的项目内相对路径：";
                actual += text;
                if (!check.detail.empty())
                {
                    actual += "（";
                    actual += check.detail;
                    actual += "）";
                }
                m_diagnostics.add(ProjectDiagnostic{ProjectCategory::PathOutsideProject,
                                                    std::string(subject),
                                                    std::string("相对项目根、POSIX 分隔、不含 '..' 与盘符的路径"),
                                                    std::move(actual),
                                                    std::string(location),
                                                    std::string("修正清单声明或生成项的目标路径后重跑（spec §2.3）")});
                return std::nullopt;
            }

            void add_generated(std::string target, std::optional<std::string> plugin)
            {
                std::optional<std::string> target_path = normalize_path(target, target, target);
                if (!target_path.has_value())
                {
                    return;
                }

                AssetFile file;
                file.target_relative = std::move(*target_path);
                file.stage           = AssetStage::Generate;
                file.plugin          = std::move(plugin);
                m_files.push_back(std::move(file));
            }

            /**
             *  @brief 登记一项必需复制文件：目标与源同形，摘要取源内容
             *  @param is_required_deliverable 该项是插件根必备交付件（`plugin_config.json` / `README.md`）：
             *         缺失或不可读按 `plugin_asset_incomplete` 报告，与清单声明条目的 `asset_missing` 分开
             *         （spec §10.6）
             *  @return 源文件是普通文件时返回 true；路径不合规报 `path_outside_project`、
             *          缺失或类型不符报 `asset_missing`（必备件报 `plugin_asset_incomplete`），
             *          两者都返回 false 且不登记该目标
             */
            bool add_copy(const std::string& target, std::optional<std::string> plugin, std::string_view subject,
                          std::string_view location, bool is_required_deliverable = false)
            {
                // 源与目标同形：同一条路径经 normalize_relative 后写入两侧，不产生第二条未经校验的路径
                const std::optional<std::string> target_path = normalize_path(target, subject, location);
                if (!target_path.has_value())
                {
                    return false;
                }

                const std::filesystem::path absolute = m_engine_root / to_native(*target_path);
                bool is_regular_ok = false;
                if (!is_regular_file(absolute, is_regular_ok))
                {
                    if (is_required_deliverable)
                    {
                        report_incomplete(subject, location, "插件根必备交付件存在且为普通文件",
                                          "源路径不存在或不是普通文件：" + *target_path);
                    }
                    else
                    {
                        report_missing(subject, location, "清单声明的条目存在且为普通文件",
                                       "源路径不存在：" + *target_path);
                    }
                    return false;
                }

                bool is_digest_ok = false;
                const Digest digest = digest_file(absolute, is_digest_ok);
                if (!is_digest_ok)
                {
                    if (is_required_deliverable)
                    {
                        report_incomplete(subject, location, "插件根必备交付件可读", "读取失败：" + *target_path);
                    }
                    else
                    {
                        report_missing(subject, location, "源文件可读", "读取失败：" + *target_path);
                    }
                    return false;
                }

                AssetFile file;
                file.target_relative = *target_path;
                file.source_relative = *target_path;
                file.digest          = digest;
                file.stage           = AssetStage::Copy;
                file.plugin          = std::move(plugin);
                m_files.push_back(std::move(file));
                return true;
            }

            /**
             *  @brief 枚举一个声明目录的整树文件
             *  @return 目录存在且可列举时返回 true；路径不合规报 `path_outside_project`、
             *          缺失报 `asset_missing`，两者都返回 false 且该目录不进目录集合
             */
            bool add_tree(const std::string& target_root, std::optional<std::string> plugin, std::string_view subject,
                          std::string_view location)
            {
                // 先规范化再读盘：形态违规的声明不去源侧枚举，避免对越界路径做任何访问
                const std::optional<std::string> root_path = normalize_path(target_root, subject, location);
                if (!root_path.has_value())
                {
                    return false;
                }

                const std::filesystem::path absolute = m_engine_root / to_native(*root_path);
                bool is_directory_ok = false;
                if (!is_directory(absolute, is_directory_ok))
                {
                    report_missing(subject, location, "清单声明的目录存在且为目录",
                                   "源路径不存在或不是目录：" + *root_path);
                    return false;
                }

                add_directory(*root_path);
                return enumerate(absolute, *root_path, plugin);
            }

            /**
             *  @brief 枚举一个共享 vendor 目录（`IDPlugin/Vendor/<dir>` 整树）
             *  @return 目录就位（含已被前面插件枚举过）时返回 true；缺失报 asset_missing
             *  @note 多个插件引用同一目录时只枚举一次：目标与内容相同，重复登记会被合并
             */
            bool add_shared_vendor_tree(const std::string& directory)
            {
                for (const std::string& known : m_enumerated_vendors)
                {
                    if (known == directory)
                    {
                        return true;
                    }
                }
                m_enumerated_vendors.push_back(directory);

                const std::string target = std::string(kVendorRoot) + "/" + directory;
                return add_tree(target, std::nullopt, directory, target);
            }

            void add_directory(std::string target)
            {
                std::optional<std::string> directory_path = normalize_path(target, target, target);
                if (!directory_path.has_value())
                {
                    return;
                }
                m_directories.push_back(std::move(*directory_path));
            }

            void report_missing(std::string_view subject, std::string_view location, std::string_view expected,
                                std::string actual)
            {
                m_diagnostics.add(ProjectDiagnostic{ProjectCategory::AssetMissing,
                                                    std::string(subject),
                                                    std::string(expected),
                                                    std::move(actual),
                                                    std::string(location),
                                                    std::string("补齐源资产或修正清单声明后重跑；"
                                                                "目录里未被声明的文件不作交付缺陷处理")});
            }

            void report_incomplete(std::string_view subject, std::string_view location, std::string_view expected,
                                   std::string actual)
            {
                m_diagnostics.add(ProjectDiagnostic{ProjectCategory::PluginAssetIncomplete,
                                                    std::string(subject),
                                                    std::string(expected),
                                                    std::move(actual),
                                                    std::string(location),
                                                    std::string("补齐插件根必备交付件后重跑")});
            }

            void report_target_conflict(const std::string& target, const AssetFile& existing, const AssetFile& incoming)
            {
                std::string actual = "目标 " + target + " 被两处声明命中：";
                actual += describe(existing);
                actual += " 与 ";
                actual += describe(incoming);
                report_incomplete(target, target, "同一目标路径的声明内容一致", std::move(actual));
            }

            AssetPlan finish()
            {
                std::sort(m_files.begin(), m_files.end(),
                          [](const AssetFile& left, const AssetFile& right) noexcept
                          {
                              return std::string_view(left.target_relative) < std::string_view(right.target_relative);
                          });

                AssetPlan plan;
                for (AssetFile& file : m_files)
                {
                    // 目标路径的父目录一并进入目录集合（保留集素材）
                    add_parent_chain(file.target_relative);

                    if (!plan.files.empty() && plan.files.back().target_relative == file.target_relative)
                    {
                        if (digest_equals(plan.files.back().digest, file.digest))
                        {
                            continue;   // 同源同内容：合并为一条
                        }
                        report_target_conflict(file.target_relative, plan.files.back(), file);
                        continue;
                    }
                    plan.files.push_back(std::move(file));
                }

                add_parent_chain(std::string(kPluginRoot));
                std::sort(m_directories.begin(), m_directories.end(),
                          [](const std::string& left, const std::string& right) noexcept
                          {
                              return std::string_view(left) < std::string_view(right);
                          });
                m_directories.erase(std::unique(m_directories.begin(), m_directories.end()), m_directories.end());

                plan.directories.reserve(m_directories.size());
                for (const std::string& directory : m_directories)
                {
                    AssetDirectory entry;
                    entry.target_relative = directory;
                    entry.plugin          = plugin_of(directory);
                    plan.directories.push_back(std::move(entry));
                }

                plan.diagnostics = m_diagnostics;
                plan.is_valid    = !m_diagnostics.has_error();
                return plan;
            }

        private:
            std::string describe(const AssetFile& file) const
            {
                std::string text = file.source_relative.has_value() ? *file.source_relative : std::string("<generated>");
                text += "（";
                text += file.digest.to_hex();
                text += "）";
                return text;
            }

            // `IDPlugin/<Plugin>/...` 的目录归属该插件；其它目录不属于任何插件
            std::optional<std::string> plugin_of(std::string_view directory) const
            {
                for (const std::string& name : m_plugin_names)
                {
                    if (std::string_view(directory) == std::string(kPluginRoot) + "/" + name)
                    {
                        return name;
                    }
                    if (starts_with(directory, std::string(kPluginRoot) + "/" + name + "/"))
                    {
                        return name;
                    }
                }
                return std::nullopt;
            }

            void add_parent_chain(const std::string& target)
            {
                std::string_view current = parent_relative(target);
                while (!current.empty())
                {
                    m_directories.emplace_back(current);
                    current = parent_relative(current);
                }
            }

            bool enumerate(const std::filesystem::path& absolute_directory, const std::string& target_directory,
                           const std::optional<std::string>& plugin)
            {
                const DirectoryList listing = list_directory(absolute_directory);
                if (!listing.is_ok)
                {
                    report_missing(target_directory, target_directory, "声明目录可列举", "目录列举失败");
                    return false;
                }

                bool is_ok = true;
                for (const std::string& name : listing.names)
                {
                    const std::filesystem::path absolute_child = absolute_directory / to_native(name);

                    // 枚举出的名字也要过同一入口：保留名、尾部空格或点等形态在磁盘上可能存在，
                    // 但它们不能成为复制目标
                    const std::optional<std::string> child_path =
                        normalize_path(target_directory + "/" + name, target_directory, target_directory);
                    if (!child_path.has_value())
                    {
                        is_ok = false;
                        continue;
                    }

                    bool is_directory_ok = false;
                    if (is_directory(absolute_child, is_directory_ok))
                    {
                        add_directory(*child_path);
                        is_ok = enumerate(absolute_child, *child_path, plugin) && is_ok;
                        continue;
                    }

                    bool is_regular_ok = false;
                    if (!is_regular_file(absolute_child, is_regular_ok))
                    {
                        // 链接或其它类型：不跟随、不复制，按缺失报告，不静默跳过
                        report_missing(*child_path, target_directory, "普通文件或目录", "不是普通文件");
                        is_ok = false;
                        continue;
                    }

                    bool is_digest_ok = false;
                    const Digest digest = digest_file(absolute_child, is_digest_ok);
                    if (!is_digest_ok)
                    {
                        report_missing(*child_path, target_directory, "源文件可读", "读取失败");
                        is_ok = false;
                        continue;
                    }

                    AssetFile file;
                    file.target_relative = *child_path;
                    file.source_relative = *child_path;
                    file.digest          = digest;
                    file.stage           = AssetStage::Copy;
                    file.plugin          = plugin;
                    m_files.push_back(std::move(file));
                }
                return is_ok;
            }

            const std::filesystem::path&       m_engine_root;
            const std::vector<std::string>&    m_plugin_names;
            DiagnosticSet&                     m_diagnostics;
            std::vector<AssetFile>             m_files        = {};
            std::vector<std::string>           m_directories  = {};
            std::vector<std::string>           m_enumerated_vendors = {};
        };

        // 单文件条目（header_dirs 的 File 种类、files、common_headers）与目录条目区分对待
        bool is_under_entry(std::string_view child, std::string_view ancestor) noexcept
        {
            if (ancestor.empty() || child.size() <= ancestor.size() + 1)
            {
                return false;
            }
            if (child.compare(0, ancestor.size(), ancestor) != 0)
            {
                return false;
            }
            return child[ancestor.size()] == '/';
        }
    } // namespace

    AssetPlan plan_assets(const std::filesystem::path& project_root, const std::filesystem::path& engine_root,
                          const Resolution& resolution, const PluginCatalog& catalog)
    {
        // 目标侧（项目内）的存在性与冲突判定归计划阶段；本函数只读源资产，因此不使用项目根做判断
        (void)project_root;

        std::vector<std::string> plugin_names;
        plugin_names.reserve(resolution.selection.all.size());
        for (const EffectivePlugin& plugin : resolution.selection.all)
        {
            plugin_names.push_back(plugin.name);
        }
        std::sort(plugin_names.begin(), plugin_names.end());

        DiagnosticSet diagnostics;
        PlanCollector collector(engine_root, plugin_names, diagnostics);

        // 聚合构建描述（生成）：IDPlugin/CMakeLists.txt
        collector.add_generated(std::string(kPluginRoot) + "/" + std::string(kCMakeFileName), std::nullopt);

        for (const EffectivePlugin& plugin : resolution.selection.all)
        {
            const CatalogEntry* entry = find_plugin(catalog, plugin.name);
            if (entry == nullptr)
            {
                continue;   // 未发现/被忽略的插件已在解析阶段报 plugin_unknown
            }
            const PluginManifest& manifest = entry->manifest;
            const std::string& plugin_root = manifest.source_root;

            // 插件根：构建描述为生成物，清单与 README 是复制项且属必备交付件——缺失或不可读一律报
            // plugin_asset_incomplete（不重复报 asset_missing，也不因此静默跳过该插件）
            collector.add_generated(plugin_root + "/" + std::string(kCMakeFileName), plugin.name);
            collector.add_copy(plugin_root + "/" + std::string(kManifestFile), plugin.name, plugin.name,
                               plugin_root + "/" + std::string(kManifestFile), true);
            collector.add_copy(plugin_root + "/" + std::string(kReadmeFile), plugin.name, plugin.name,
                               plugin_root + "/" + std::string(kReadmeFile), true);

            // header_dirs：目录条目整树；单文件条目只该文件；同目录同时有单文件条目时收敛为被列出的文件
            for (const HeaderDirEntry& header : manifest.header_dirs)
            {
                const std::string target = plugin_root + "/" + header.path;
                if (header.kind == HeaderDirKind::File)
                {
                    collector.add_copy(target, plugin.name, plugin.name, target);
                    continue;
                }

                bool has_file_entry = false;
                for (const HeaderDirEntry& other : manifest.header_dirs)
                {
                    if (other.kind == HeaderDirKind::File && is_under_entry(other.path, header.path))
                    {
                        has_file_entry = true;
                        break;
                    }
                }
                if (has_file_entry)
                {
                    // 目录只承担 include 根语义：复制与保留范围收敛为被列出的文件集合
                    collector.add_directory(target);
                    continue;
                }
                collector.add_tree(target, plugin.name, plugin.name, target);
            }

            for (const std::string& directory : manifest.source_dirs)
            {
                const std::string target = plugin_root + "/" + directory;
                collector.add_tree(target, plugin.name, plugin.name, target);
            }

            for (const std::string& directory : manifest.vendor_dirs)
            {
                const std::string target = plugin_root + "/" + directory;
                collector.add_tree(target, plugin.name, plugin.name, target);
            }

            for (const SharedVendor& vendor : manifest.shared_vendors)
            {
                collector.add_shared_vendor_tree(vendor.dir);
            }

            for (const std::string& file : manifest.files)
            {
                const std::string target = plugin_root + "/" + file;
                collector.add_copy(target, plugin.name, plugin.name, target);
            }
        }

        // common_headers 并集：只保留被真正引用的头，不复制整棵树
        for (const std::string& header : resolution.common_headers)
        {
            const std::string target = std::string(kCommonRoot) + "/" + header;
            collector.add_copy(target, std::nullopt, std::string(kCommonRoot), target);
        }

        return collector.finish();
    }
} // namespace ID::Project
