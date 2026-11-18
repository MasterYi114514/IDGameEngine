#include "project/Plan/Retention.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "project/Base/PathText.hpp"
#include "project/Base/Text.hpp"
#include "project/Plan/PathDigest.hpp"
#include "project/Record/RecordPaths.hpp"

/**
 *  保留集的两个入口与裁剪判定。
 *
 *  保留集只增不减地由清单声明决定：`build_retention` 从资产计划取路径，`retention_from_record` 只做
 *  载体转换；两者都不扫描目录推断"该保留什么"。裁剪阶段的目录扫描只服务一件事：找出待删范围内的
 *  未登记路径并阻断整次裁剪（spec §8.8），不据此删除任何东西。
 */

namespace ID::Project
{
    namespace
    {
        constexpr std::string_view kFragmentOutputDirs    = "output-dirs";
        constexpr std::string_view kFragmentPluginTargets = "plugin-targets";

        // 目录递归上限：超过即停止枚举（未登记扫描只在有候选时进行，正常项目远小于该深度）
        constexpr std::size_t kMaxScanDepth = 32;

        // 条目排序键：先路径、再片段（空片段排在同名片段之前）
        bool is_before_entry(std::string_view left_path, std::string_view left_fragment, std::string_view right_path,
                             std::string_view right_fragment) noexcept
        {
            if (left_path != right_path)
            {
                return left_path < right_path;
            }
            return left_fragment < right_fragment;
        }

        std::string join_common_header(std::string_view relative)
        {
            std::string path(kCommonRoot);
            path += '/';
            path += relative;
            return path;
        }

        bool is_seed_entry(const ManifestEntry& entry) noexcept
        {
            return entry.kind == ManifestKind::Seed;
        }

        bool is_same_identity(const ManifestEntry& entry, std::string_view path,
                              const std::optional<std::string>& fragment) noexcept
        {
            if (entry.path != path)
            {
                return false;
            }
            if (entry.fragment.has_value() != fragment.has_value())
            {
                return false;
            }
            return !fragment.has_value() || *entry.fragment == *fragment;
        }

        // 递归枚举一个目录下的全部文件路径（相对项目根）；深度或可读性出问题时停止该分支，不上报失败。
        // 命中重解析点的目录不跟随：它可能指向项目外，枚举它既越界也会凭空造出"未登记路径"
        void collect_files(const std::filesystem::path& project_root, std::string_view relative_directory,
                           std::size_t depth, std::vector<std::string>& files,
                           std::vector<std::string>& blocked_links)
        {
            if (depth > kMaxScanDepth)
            {
                return;
            }

            const std::filesystem::path absolute = project_root / to_native(relative_directory);
            const DirectoryList listing = list_directory(absolute);
            if (!listing.is_ok)
            {
                return;
            }

            for (const std::string& name : listing.names)
            {
                std::string child(relative_directory);
                if (!child.empty())
                {
                    child += '/';
                }
                child += name;

                bool is_query_ok = false;
                const bool child_is_directory = is_directory(project_root / to_native(child), is_query_ok);
                if (!is_query_ok)
                {
                    continue;
                }
                if (child_is_directory)
                {
                    if (check_reparse_points_under(project_root / to_native(child), project_root).verdict
                        != PathVerdict::Ok)
                    {
                        blocked_links.push_back(std::move(child));
                        continue;
                    }
                    collect_files(project_root, child, depth + 1, files, blocked_links);
                    continue;
                }
                files.push_back(std::move(child));
            }
        }

        ProjectDiagnostic make_cleanup_diagnostic(std::string subject, std::string expected, std::string actual,
                                                  std::string location, std::string next_action)
        {
            return ProjectDiagnostic{ProjectCategory::CleanupBlocked, std::move(subject), std::move(expected),
                                     std::move(actual), std::move(location), std::move(next_action)};
        }

        // 裁剪目标的路径安全：祖先链上任一段命中重解析点即终止（spec §2.3、§8.8）。
        // 判在删除判定之前：越界不是"未登记路径"冲突，任何 `--force` 都不放行。
        bool is_trim_target_inside_project(const std::filesystem::path& project_root, std::string_view relative,
                                           TrimPlan& plan)
        {
            const std::filesystem::path absolute = project_root / to_native(relative);
            const PathCheck             check    = check_reparse_points_under(absolute, project_root);
            if (check.verdict == PathVerdict::Ok)
            {
                return true;
            }

            const bool is_link = check.verdict == PathVerdict::ReparsePoint;
            plan.is_valid      = false;
            plan.diagnostics.add(ProjectDiagnostic{
                ProjectCategory::PathOutsideProject,
                std::string(relative),
                "裁剪目标的祖先链不含符号链接、junction 或挂载点",
                is_link ? check.detail : "路径段属性查询失败：" + to_utf8(absolute),
                std::string(relative),
                "把该路径上的链接换成项目内的真实目录后重跑；工具不跟随链接读写项目外（spec §2.3）"});
            return false;
        }
    } // namespace

    bool RetentionSet::contains_path(std::string_view relative) const noexcept
    {
        const auto found = std::lower_bound(m_paths.begin(), m_paths.end(), relative,
                                           [](const std::string& entry, std::string_view value) noexcept
                                           {
                                               return std::string_view(entry) < value;
                                           });
        return found != m_paths.end() && std::string_view(*found) == relative;
    }

    bool RetentionSet::contains_entry(std::string_view relative, std::optional<std::string_view> fragment) const noexcept
    {
        if (!contains_path(relative))
        {
            return false;
        }
        if (!fragment.has_value())
        {
            return true;
        }

        const auto found = std::lower_bound(m_entries.begin(), m_entries.end(), *fragment,
                                            [relative](const std::pair<std::string, std::string>& entry,
                                                       std::string_view value) noexcept
                                            {
                                                if (std::string_view(entry.first) != relative)
                                                {
                                                    return std::string_view(entry.first) < relative;
                                                }
                                                return std::string_view(entry.second) < value;
                                            });
        if (found == m_entries.end())
        {
            return false;
        }
        return std::string_view(found->first) == relative && std::string_view(found->second) == *fragment;
    }

    const std::vector<std::string>& RetentionSet::paths() const noexcept
    {
        return m_paths;
    }

    void RetentionSet::add_path(std::string relative)
    {
        if (relative.empty())
        {
            return;
        }

        const auto position = std::lower_bound(m_paths.begin(), m_paths.end(), relative,
                                               [](const std::string& entry, const std::string& value) noexcept
                                               {
                                                   return std::string_view(entry) < std::string_view(value);
                                               });
        if (position != m_paths.end() && *position == relative)
        {
            return;
        }
        m_paths.insert(position, std::move(relative));
    }

    void RetentionSet::add_entry(std::string relative, std::optional<std::string> fragment)
    {
        if (!fragment.has_value())
        {
            add_path(std::move(relative));
            return;
        }

        const std::string path = relative;
        add_path(std::move(relative));

        const auto key = std::make_pair(path, *fragment);
        const auto position = std::lower_bound(m_entries.begin(), m_entries.end(), key,
                                               [](const std::pair<std::string, std::string>& left,
                                                  const std::pair<std::string, std::string>& right) noexcept
                                               {
                                                   return is_before_entry(left.first, left.second, right.first,
                                                                          right.second);
                                               });
        if (position != m_entries.end() && *position == key)
        {
            return;
        }
        m_entries.insert(position, key);
    }

    RetentionSet build_retention(const AssetPlan& assets, const Resolution& resolution)
    {
        RetentionSet retention;

        for (const AssetFile& file : assets.files)
        {
            retention.add_path(file.target_relative);
        }
        for (const AssetDirectory& directory : assets.directories)
        {
            retention.add_path(directory.target_relative);
        }
        for (const std::string& header : resolution.common_headers)
        {
            retention.add_path(join_common_header(header));
        }

        // 根 CMakeLists.txt 的两个受控片段：路径各出现一次，片段身份另立条目
        retention.add_entry(std::string(kRootCmake), std::string(kFragmentOutputDirs));
        retention.add_entry(std::string(kRootCmake), std::string(kFragmentPluginTargets));

        // seed 只记路径：不复制、不删除，也不参与裁剪判定
        retention.add_path(std::string(kSeedRelative));

        return retention;
    }

    RetentionSet retention_from_record(const ResolvedRecord& record)
    {
        RetentionSet retention;
        for (const std::string& path : record.retention)
        {
            retention.add_path(path);
        }
        return retention;
    }

    TrimPlan plan_trim(const ManifestRecord& old_manifest, const RetentionSet& retention,
                       const std::filesystem::path& project_root, bool has_force)
    {
        TrimPlan plan;

        for (const ManifestEntry& entry : old_manifest.entries)
        {
            if (is_seed_entry(entry))
            {
                continue;
            }
            if (!is_under(entry.path, kPluginRoot))
            {
                continue;
            }
            if (retention.contains_entry(entry.path, entry.fragment.has_value()
                                                        ? std::optional<std::string_view>(*entry.fragment)
                                                        : std::nullopt))
            {
                continue;
            }

            if (!is_trim_target_inside_project(project_root, entry.path, plan))
            {
                continue;
            }

            const PathDigest disk = probe_file_digest(project_root / to_native(entry.path));
            if (!disk.is_ok)
            {
                plan.is_valid = false;
                plan.diagnostics.add(make_cleanup_diagnostic(
                    entry.path, "能读取该受管文件的当前内容", "属性查询或读取失败", entry.path,
                    "确认该文件未被其它进程占用或有读权限后重跑"));
                continue;
            }

            std::optional<Digest> record_digest;
            if (!entry.digest.empty())
            {
                Digest parsed;
                if (digest_from_hex(entry.digest, parsed))
                {
                    record_digest = parsed;
                }
                else
                {
                    plan.is_valid = false;
                    plan.diagnostics.add(ProjectDiagnostic{ProjectCategory::RecordInvalid, entry.path,
                                                           "64 位十六进制摘要", entry.digest, entry.path,
                                                           "修正 manifest.json 的摘要后重跑（spec §13.2）"});
                    continue;
                }
            }

            OwnershipInput input;
            input.disk      = disk.digest;
            input.record    = record_digest;
            input.planned   = std::nullopt;
            input.has_force = has_force;
            const OwnershipVerdict verdict = judge_ownership(input);

            if (verdict == OwnershipVerdict::DeleteAlreadyGone || verdict == OwnershipVerdict::NoChange
                || verdict == OwnershipVerdict::RefreshIdentical)
            {
                continue;
            }
            if (verdict == OwnershipVerdict::DeleteNeedsForce || verdict == OwnershipVerdict::BlockModifiedLocal
                || verdict == OwnershipVerdict::BlockUnregistered)
            {
                plan.blocked_modified.push_back(entry);
                plan.diagnostics.add(make_cleanup_diagnostic(
                    entry.path,
                    "受管文件的当前内容与 manifest 登记一致（或给 --force 先备份再删）",
                    "当前摘要与 manifest 摘要不符", entry.path,
                    "先确认该文件的本地修改是否要保留：要保留则把它移出 IDPlugin/**，要放弃则加 --force 重跑"));
                continue;
            }

            DeletionCandidate candidate;
            candidate.entry        = entry;
            candidate.verdict      = verdict;
            candidate.disk_digest  = disk.digest.has_value() ? disk.digest->to_hex() : std::string();
            candidate.needs_backup = verdict == OwnershipVerdict::DeleteForced;
            plan.candidates.push_back(std::move(candidate));
        }

        // 未登记路径扫描：IDPlugin 子树内不在旧 manifest、也不在保留集里的文件即未登记路径。
        // 与是否已有待删候选无关——保留集内的未登记文件同样不可信，`clean` 没有待删项时也要阻断
        std::vector<std::string> files;
        std::vector<std::string> blocked_links;
        if (is_trim_target_inside_project(project_root, kPluginRoot, plan))
        {
            collect_files(project_root, kPluginRoot, 0, files, blocked_links);
        }
        for (const std::string& blocked : blocked_links)
        {
            // 越界目录已在扫描时跳过，这里补出诊断：不跟随链接读到项目外
            is_trim_target_inside_project(project_root, blocked, plan);
        }
        std::sort(files.begin(), files.end());

        for (const std::string& file : files)
        {
            const bool is_registered = std::any_of(old_manifest.entries.begin(), old_manifest.entries.end(),
                                                   [&file](const ManifestEntry& entry) noexcept
                                                   {
                                                       return is_same_identity(entry, file, std::nullopt);
                                                   });
            if (is_registered)
            {
                continue;
            }
            if (retention.contains_path(file))
            {
                continue;
            }
            plan.unregistered.push_back(file);
        }

        // 未登记路径的冲突类别按命令取（写入型命令是 `unmanaged_content`、`clean` 是 `cleanup_blocked`），
        // 因此诊断由调用方在成生计划时统一产出，本单元只给集合与阻断标志
        if (!plan.unregistered.empty())
        {
            plan.is_blocked = true;
        }

        std::sort(plan.candidates.begin(), plan.candidates.end(),
                  [](const DeletionCandidate& left, const DeletionCandidate& right) noexcept
                  {
                      return is_before_entry(left.entry.path, left.entry.fragment.value_or(std::string()),
                                             right.entry.path, right.entry.fragment.value_or(std::string()));
                  });

        return plan;
    }
} // namespace ID::Project
