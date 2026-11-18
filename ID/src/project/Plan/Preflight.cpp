#include "project/Plan/Preflight.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cmake/File/FragmentWriter.hpp"
#include "project/Base/Digest.hpp"
#include "project/Base/FileIo.hpp"
#include "project/Base/PathText.hpp"
#include "project/Base/Text.hpp"
#include "project/Input/PluginManifest.hpp"
#include "project/Plan/Ownership.hpp"
#include "project/Plan/PathDigest.hpp"
#include "project/Record/RecordJson.hpp"
#include "project/Record/RecordPaths.hpp"
#include "project/Resolve/Closure.hpp"
#include "project/Resolve/EffectiveSelection.hpp"
#include "project/Resolve/Generation.hpp"
#include "project/Resolve/Naming.hpp"

/**
 *  P0 的判定实现：登记与路径身份、输入可用性、记录可信性、`intent` 收敛窗口与 `build` 的离线复核。
 *
 *  判定顺序即 spec §8.3 的检查项顺序；其中 `intent` 收敛的六条按 U5 §5.8 的表序执行：
 *  先是"接管命令"判定（`new`/`update`/`clean` 只接手同命令的 attempt），再是"已提交待收尾"，最后是
 *  输入摘要与逐项内容。先判命令是表序要求：属于其它命令的已提交 attempt 不能由本次命令收尾。
 *
 *  本文件只读：不建目录、不建锁文件、不写记录、不写日志文件。
 */

namespace ID::Project
{
    namespace
    {
        constexpr std::string_view kSelectionFile = "plugins.json";
        constexpr std::string_view kRegisterEntry = "ID/project.json";

        ProjectDiagnostic make_diagnostic(ProjectCategory category, std::string subject, std::string expected,
                                          std::string actual, std::string location, std::string next_action)
        {
            return ProjectDiagnostic{category, std::move(subject), std::move(expected), std::move(actual),
                                     std::move(location), std::move(next_action)};
        }

        // 父路径是否是子路径的前缀（含相等）；Windows 上按 ASCII 折叠比较段名
        bool is_same_or_nested(const std::filesystem::path& parent, const std::filesystem::path& child)
        {
            auto parent_part = parent.begin();
            auto child_part  = child.begin();
            for (; parent_part != parent.end(); ++parent_part, ++child_part)
            {
                if (child_part == child.end())
                {
                    return false;
                }
                if (!ascii_iequals(to_utf8(*parent_part), to_utf8(*child_part)))
                {
                    return false;
                }
            }
            return true;
        }

        std::optional<std::filesystem::path> environment_path(const char* name)
        {
            const char* value = std::getenv(name);
            if (value == nullptr || *value == '\0')
            {
                return std::nullopt;
            }
            return to_native(value);
        }

        // 卷根、用户主目录与系统目录：不允许作为项目根（spec §2.2）
        bool is_special_directory(const std::filesystem::path& root)
        {
            const std::filesystem::path drive_root = root.root_path();
            if (!drive_root.empty() && is_same_or_nested(drive_root, root) && is_same_or_nested(root, drive_root))
            {
                return true;
            }

            const std::optional<std::filesystem::path> home =
                environment_path("USERPROFILE").has_value() ? environment_path("USERPROFILE")
                                                            : environment_path("HOME");
            if (home.has_value() && is_same_or_nested(*home, root) && is_same_or_nested(root, *home))
            {
                return true;
            }

            const std::optional<std::filesystem::path> system_root = environment_path("SystemRoot");
            if (system_root.has_value() && is_same_or_nested(*system_root, root) && is_same_or_nested(root, *system_root))
            {
                return true;
            }
            return false;
        }

        IntentCommand to_intent_command(CommandKind command) noexcept
        {
            switch (command)
            {
                case CommandKind::New:     return IntentCommand::New;
                case CommandKind::Clean:   return IntentCommand::Clean;
                case CommandKind::Recover: return IntentCommand::Recover;
                default:                   return IntentCommand::Update;
            }
        }

        void append_diagnostics(DiagnosticSet& target, const DiagnosticSet& source)
        {
            for (const ProjectDiagnostic& diagnostic : source.span())
            {
                target.add(diagnostic);
            }
        }

        // 只用 resolved.snapshot 重建"已接受的清单视图"：不读源插件目录，也不读 plugins.json 原始字节
        PluginCatalog build_accepted_catalog(const ResolvedRecord& resolved)
        {
            PluginCatalog catalog;
            catalog.is_ok = true;
            catalog.entries.reserve(resolved.snapshot.size());

            for (const ResolvedManifestSnapshot& snapshot : resolved.snapshot)
            {
                PluginManifest manifest;
                manifest.name            = snapshot.plugin;
                manifest.dependencies    = snapshot.dependencies;
                manifest.manifest_digest = snapshot.manifest_digest;
                manifest.source_root     = std::string(kPluginRoot) + '/' + snapshot.plugin;

                Requirement requirement = Requirement::Any;
                if (!snapshot.requirements.empty())
                {
                    Requirement parsed = Requirement::Any;
                    if (parse_requirement(snapshot.requirements.front(), parsed))
                    {
                        requirement = parsed;
                    }
                }
                manifest.requirement = requirement;

                for (const ResolvedOptionSnapshot& option : snapshot.options)
                {
                    ManifestOption declared;
                    declared.name          = option.name;
                    declared.is_bool       = option.type == "bool";
                    declared.default_on    = option.default_on;
                    declared.description   = option.description;
                    declared.define        = option.define;
                    declared.links_when_on = option.links_when_on;
                    manifest.options.push_back(std::move(declared));
                }

                CatalogEntry entry;
                entry.manifest = std::move(manifest);
                catalog.entries.push_back(std::move(entry));
            }
            return catalog;
        }

        /**
         *  展开有效选择并算规范化摘要。`failure` 非空时说明为什么算不出来（直接选择非法、闭包成环等）。
         */
        std::optional<Digest> digest_of_selection(const SelectionFile& selection, const PluginCatalog& catalog,
                                                  std::string& failure)
        {
            const EffectiveSelection expanded = expand_direct_selection(selection, catalog);
            if (!expanded.is_valid)
            {
                for (const ProjectDiagnostic& diagnostic : expanded.diagnostics.span())
                {
                    if (!is_warning(diagnostic.category))
                    {
                        failure = diagnostic.subject + "：" + diagnostic.actual;
                        return std::nullopt;
                    }
                }
                failure = "直接选择无法展开";
                return std::nullopt;
            }

            const Resolution resolution = resolve_closure(expanded, catalog);
            if (!resolution.is_valid)
            {
                for (const ProjectDiagnostic& diagnostic : resolution.diagnostics.span())
                {
                    if (!is_warning(diagnostic.category))
                    {
                        failure = diagnostic.subject + "：" + diagnostic.actual;
                        return std::nullopt;
                    }
                }
                failure = "依赖闭包无法解析";
                return std::nullopt;
            }

            return compute_selection_digest(resolution.selection);
        }

        // 差异的人读说明：插件名 + 字段，最多三条
        std::string describe_selection_difference(const EffectiveSelection& current, const ResolvedRecord& resolved)
        {
            std::string detail;
            std::size_t described = 0;

            const auto append_detail = [&detail, &described](const std::string& text)
            {
                if (described >= 3)
                {
                    return;
                }
                if (!detail.empty())
                {
                    detail += "；";
                }
                detail += text;
                ++described;
            };

            for (const EffectivePlugin& plugin : current.all)
            {
                const auto found = std::find_if(resolved.selection.begin(), resolved.selection.end(),
                                                [&plugin](const ResolvedSelectionEntry& entry) noexcept
                                                {
                                                    return entry.plugin == plugin.name;
                                                });
                if (found == resolved.selection.end())
                {
                    append_detail(plugin.name + "：快照里没有该插件（直接选择集合已变）");
                    continue;
                }
                if (found->linkage != to_string(plugin.linkage))
                {
                    append_detail(plugin.name + "：linkage 由 " + found->linkage + " 变为 "
                                  + std::string(to_string(plugin.linkage)));
                }
                if (found->is_direct != plugin.is_direct)
                {
                    append_detail(plugin.name + "：直接选择标志已变");
                }
                if (found->option_names.size() != plugin.options.size())
                {
                    append_detail(plugin.name + "：选项集合与快照不同");
                    continue;
                }
                for (std::size_t index = 0; index < plugin.options.size(); ++index)
                {
                    const std::string value = plugin.options[index].value ? "ON" : "OFF";
                    if (found->option_names[index] != plugin.options[index].name
                        || found->option_values[index] != value)
                    {
                        append_detail(plugin.name + "：选项 " + plugin.options[index].name + " 的取值已变");
                        break;
                    }
                }
            }

            for (const ResolvedSelectionEntry& entry : resolved.selection)
            {
                const auto found = std::find_if(current.all.begin(), current.all.end(),
                                                [&entry](const EffectivePlugin& plugin) noexcept
                                                {
                                                    return plugin.name == entry.plugin;
                                                });
                if (found == current.all.end())
                {
                    append_detail(entry.plugin + "：当前选择里已没有该插件");
                }
            }

            return detail;
        }

        // 当前对象摘要：整文件条目取整文件字节，片段条目取片段文本范围；不存在时 digest 为空
        PathDigest current_object_digest(const std::filesystem::path& project_root, std::string_view path,
                                         const std::optional<std::string>& fragment,
                                         ID::CMake::FragmentWriter& writer)
        {
            if (!fragment.has_value())
            {
                return probe_file_digest(project_root / to_native(path));
            }

            PathDigest result;
            const std::string_view names[1] = {*fragment};
            const ID::CMake::FragmentSnapshot snapshot = writer.read_fragments(std::string(path), names);
            if (snapshot.diagnostic.has_value())
            {
                result.is_ok = false;
                return result;
            }
            if (snapshot.state != ID::CMake::FragmentState::Ok)
            {
                // 标记缺失即该片段当前不存在；不完整、重复或错序都无法建立可证关系
                if (snapshot.state != ID::CMake::FragmentState::Missing)
                {
                    result.is_ok = false;
                }
                return result;
            }
            for (const ID::CMake::FragmentRange& range : snapshot.ranges)
            {
                if (range.name != *fragment)
                {
                    continue;
                }
                const std::string hex = ID::CMake::fragment_range_digest(snapshot.file_bytes, range);
                Digest parsed;
                if (hex.empty() || !digest_from_hex(hex, parsed))
                {
                    result.is_ok = false;
                    return result;
                }
                result.digest = parsed;
                return result;
            }
            return result;
        }

        bool matches_digest_state(const PathDigest& current, const DigestState& expected)
        {
            std::optional<Digest> expected_value;
            if (!digest_state_value(expected, expected_value))
            {
                return false;
            }
            if (current.digest.has_value() != expected_value.has_value())
            {
                return false;
            }
            return !current.digest.has_value() || current.digest->bytes == expected_value->bytes;
        }

        /**
         *  "内容项是否已全部落盘"的判定结果：`is_ok` 为假表示摘要不可证（读取失败或标记异常）。
         */
        struct CompletionCheck
        {
            bool is_ok       = true;    // 每个内容项的当前摘要都读到了
            bool is_complete = false;   // 全部内容项当前等于该项 after
        };

        // 内容项（U5 §5.8）：intent.operations 里 kind 为 write / delete 的项（payload 粒度由 payload_type 决定）
        CompletionCheck has_only_completed_content(const std::filesystem::path& project_root,
                                                   const IntentRecord& intent, ID::CMake::FragmentWriter& writer)
        {
            CompletionCheck check;
            check.is_complete = true;

            for (const IntentOperation& operation : intent.operations)
            {
                if (operation.kind != OperationKind::Write && operation.kind != OperationKind::Delete)
                {
                    continue;
                }

                const PathDigest current =
                    current_object_digest(project_root, operation.path, operation.fragment, writer);
                if (!current.is_ok)
                {
                    check.is_ok       = false;
                    check.is_complete = false;
                    return check;
                }
                if (!matches_digest_state(current, operation.after))
                {
                    check.is_complete = false;
                }
            }
            return check;
        }

        /**
         *  最小 bootstrap 残留：目录里只有一层空的 `IDGenerated/`。首次 `new` 在写 `intent` 之前失败（含进程被杀）
         *  会留下这层空目录，它既不是"已提交组装"，也不构成"目录已被用户占用"，因此不算目录非空：
         *  重跑同一 `new` 必须能继续（spec §6.4；矩阵 F0）。非空或含其它任何条目一律仍按 `project_exists` 拒绝。
         */
        bool is_only_bootstrap_residue(const std::filesystem::path& project_root, const DirectoryList& listing)
        {
            constexpr std::string_view kGeneratedDirectory = "IDGenerated";
            if (listing.names.size() != 1 || listing.names.front() != kGeneratedDirectory)
            {
                return false;
            }
            const DirectoryList generated = list_directory(project_root / std::string(kGeneratedDirectory));
            return generated.is_ok && generated.names.empty();
        }

        /**
         *  `manifest` 条目路径的安全校验：先判形态（越界、盘符、`..`、ADS、保留名、非 POSIX 形态），
         *  再做 `entries_digest` 与 `generation_id` 的自洽判定（spec §13.2 `path_outside_project`）。
         *  安全类校验不得被摘要校验掩盖：篡改过的条目路径本身就是越界证据，不能只报"摘要不符"。
         *  @param manifest 已解析出的记录（字段级非法时条目为空，由解析诊断负责）
         *  @param diagnostics 追加逐条诊断
         *  @return 命中任意非法条目时返回真
         *  @note 只读：不访问条目指向的磁盘位置，也不据此删除任何文件
         */
        bool report_illegal_manifest_paths(const ManifestRecord& manifest, DiagnosticSet& diagnostics)
        {
            bool has_illegal = false;
            for (const ManifestEntry& entry : manifest.entries)
            {
                const PathCheck check = normalize_relative(entry.path);
                if (check.verdict == PathVerdict::Ok)
                {
                    continue;
                }

                std::string actual = "非法的项目内相对路径：";
                actual += entry.path;
                if (!check.detail.empty())
                {
                    actual += "（";
                    actual += check.detail;
                    actual += "）";
                }

                diagnostics.add(make_diagnostic(
                    ProjectCategory::PathOutsideProject, entry.path,
                    "记录条目是相对项目根、POSIX 分隔、不含 '..' 与盘符的路径", std::move(actual),
                    std::string(kManifestRelative),
                    "该条目属越界或非法形态：修正 manifest.json 后重跑；需要重新组装则先用 recover 收敛该 attempt"));
                has_illegal = true;
            }
            return has_illegal;
        }

        // 记录对是否落在"提交未完成窗口"的形态：只有 generation 与 manifest_digest 层面的错配可能正是未完成的
        // 提交造成的。其余自洽性错误不可能由该窗口解释，因此在这里就地复算：两份记录各自的自洽性（schema、
        // 条目数与 `entries_digest`、条目 `generation_id` 与顶层一致、(path, fragment) 身份唯一）任一不成立即否
        bool is_pending_commit_shape(const ResolvedRecord& resolved, const ManifestRecord& manifest,
                                     std::string_view manifest_bytes)
        {
            if (check_resolved_self(resolved).has_value() || check_manifest_self(manifest).has_value())
            {
                return false;
            }
            if (resolved.schema_version != manifest.schema_version)
            {
                return false;
            }
            const bool is_same_generation = resolved.generation_id == manifest.generation_id;
            const bool is_same_manifest   = resolved.manifest_digest == digest_text(manifest_bytes).to_hex();
            return !is_same_generation || !is_same_manifest;
        }

        // 当前记录是否等于某个快照：`absent` 快照要求当前也不存在
        bool matches_snapshot(const RecordSnapshot& snapshot, bool has_current, std::string_view current_digest)
        {
            if (!snapshot.is_present)
            {
                return !has_current;
            }
            return has_current && current_digest == snapshot.digest;
        }

        /**
         *  base 快照与当前记录的可证关系：当前等于 base，**或**已等于本次 attempt 的目标摘要。
         *  后一种正是"manifest 已替换、resolved 未替换"（或反之）的提交未完成窗口：它由 intent 的目标快照
         *  证明，不属于记录损坏（spec §6.6、§8.9）。
         */
        bool is_base_provable(const RecordReadings& readings, const IntentRecord& intent,
                              std::string_view resolved_digest_hex, std::string_view manifest_digest_hex)
        {
            const bool resolved_ok =
                matches_snapshot(intent.base_resolved, readings.has_resolved, resolved_digest_hex)
                || matches_snapshot(intent.target_resolved, readings.has_resolved, resolved_digest_hex);
            const bool manifest_ok =
                matches_snapshot(intent.base_manifest, readings.has_manifest, manifest_digest_hex)
                || matches_snapshot(intent.target_manifest, readings.has_manifest, manifest_digest_hex);
            return resolved_ok && manifest_ok;
        }

        void judge_intent(const PreflightRequest& request, const std::string& resolved_digest_hex,
                          const std::string& manifest_digest_hex, PreflightResult& result,
                          ID::CMake::FragmentWriter& writer)
        {
            RecordReadings&     readings   = result.records;
            const IntentRecord& intent     = readings.intent;
            const bool          is_restore = request.command == CommandKind::Recover && request.has_restore_action;

            // 1. 接管命令判定（U5 §5.8 第一行，先于"已提交待收尾"）：new/update/clean 只接手同命令的
            //    attempt，命令不同即拒绝接管；`recover` 是唯一的跨命令收敛入口，不比较命令
            if (request.command != CommandKind::Recover && to_intent_command(request.command) != intent.command)
            {
                result.diagnostics.add(make_diagnostic(
                    ProjectCategory::RecoveryConflict, std::string(kIntentRelative),
                    "由本次命令接手的 attempt",
                    "attempt 的 command 与本次命令不同（见 intent.json 的 command 字段）", std::string(kIntentRelative),
                    "运行 intent.next_argv 里的原命令续做，或 recover <name> --action restore 撤销本次改动"));
                return;
            }

            // 2. 已提交窗口：resolved 当前摘要等于目标摘要时组装已提交，只差收尾
            if (readings.is_committed_window)
            {
                if (is_restore)
                {
                    result.diagnostics.add(make_diagnostic(
                        ProjectCategory::RecoveryConflict, std::string(kIntentRelative),
                        "restore 只用于尚未提交的 attempt",
                        "组装已提交（resolved 已等于本次 attempt 的目标摘要），restore 不可用", std::string(kIntentRelative),
                        "收尾只能由 recover --action continue 完成；要重新组装则运行 update"));
                    return;
                }
                if (request.command == CommandKind::Build)
                {
                    result.diagnostics.add(make_diagnostic(
                        ProjectCategory::RecoveryConflict, std::string(kIntentRelative),
                        "没有未完成的 attempt",
                        "组装已提交但 intent 尚未收尾（P7 要求没有未完成的 intent）", std::string(kIntentRelative),
                        "先运行 recover " + request.project_name + " --action continue 完成收尾，再运行 build"));
                    return;
                }
                readings.is_intent_window = true;    // 记录错配已由"已提交待收尾"解释
                return;
            }

            // 3. build 遇任何未完成的 intent 都拒绝进入 P7
            if (request.command == CommandKind::Build)
            {
                result.diagnostics.add(make_diagnostic(
                    ProjectCategory::RecoveryConflict, std::string(kIntentRelative),
                    "没有未完成的 attempt", "存在未完成的 attempt（command 见 intent.json）", std::string(kIntentRelative),
                    "先运行 intent.next_argv 里的原命令续做，或 recover <name> --action restore 撤销本次改动"));
                return;
            }

            // 4. base 或目标快照必须与磁盘建立可证关系，否则删除范围不可推导
            if (!is_base_provable(readings, intent, resolved_digest_hex, manifest_digest_hex))
            {
                result.diagnostics.add(make_diagnostic(
                    ProjectCategory::RecordInvalid, std::string(kIntentRelative),
                    "intent 的 base 或目标快照与当前两份记录一致", "两者都不能与磁盘记录建立可证关系",
                    std::string(kIntentRelative),
                    "不要依赖本次 attempt 推导删除范围；确认 intent.json 来源后删除它并重跑 update"));
                return;
            }
            readings.is_intent_window = true;    // 记录错配已由"由 intent 快照可证"解释，不再报 record_invalid

            // 5. 输入摘要：选择已变则不能续做。restore 不做输入前置（输入已变时它正是收敛入口）；
            //    clean 不读 plugins.json，也就没有可比的输入摘要（spec §6.6、§9.3 链路 4）
            if (!is_restore && request.command != CommandKind::Clean)
            {
                if (!result.has_selection)
                {
                    result.diagnostics.add(make_diagnostic(
                        ProjectCategory::RecoveryConflict, std::string(kIntentRelative),
                        "能复算本次 attempt 的输入摘要",
                        "项目内没有可读的 plugins.json，输入摘要不可证", std::string(kSelectionFile),
                        "恢复 plugins.json 后重跑；或运行 recover " + request.project_name
                            + " --action restore 撤销本次改动"));
                    return;
                }

                std::optional<Digest> selection_digest;
                const bool is_offline = request.command == CommandKind::Build
                                     || request.command == CommandKind::Recover
                                     || (request.command == CommandKind::New && readings.is_trusted);
                if (is_offline)
                {
                    // 只用本次 attempt 的校验快照：不读 IDPlugin 源清单，源插件库整体不可用时同样成立
                    const RecordRead<ResolvedRecord> accepted = parse_resolved(intent.target_resolved.bytes);
                    if (!accepted.is_ok)
                    {
                        result.diagnostics.add(make_diagnostic(
                            ProjectCategory::RecordInvalid, std::string(kIntentRelative),
                            "intent 的目标 resolved 可解析", "目标 resolved 无法解析", std::string(kIntentRelative),
                            "确认 intent.json 未被外部改写；必要时按恢复流程处置该 attempt"));
                        return;
                    }
                    const OfflineSelection evaluated = evaluate_offline_selection(result.selection, accepted.value);
                    if (!evaluated.is_ok)
                    {
                        result.diagnostics.add(make_diagnostic(
                            ProjectCategory::RecoveryConflict, std::string(kIntentRelative),
                            "attempt 的输入摘要与当前选择一致",
                            "无法按已接受的清单视图复算当前选择（输入摘要不可证）", std::string(kSelectionFile),
                            "先运行 recover " + request.project_name
                                + " --action restore 撤销本次改动，再按当前选择运行 update"));
                        return;
                    }
                    selection_digest = evaluated.digest;
                }
                else
                {
                    std::string failure;
                    selection_digest = digest_of_selection(result.selection, result.catalog, failure);
                    if (!selection_digest.has_value())
                    {
                        result.diagnostics.add(make_diagnostic(
                            ProjectCategory::ProjectConfigInvalid, std::string(kSelectionFile),
                            "能按当前 plugins.json 复算规范化有效选择", failure, std::string(kSelectionFile),
                            "修正 plugins.json 的插件名、linkage_type 与选项取值后重跑"));
                        return;
                    }
                }

                if (selection_digest->to_hex() != intent.selection_digest)
                {
                    result.diagnostics.add(make_diagnostic(
                        ProjectCategory::RecoveryConflict, std::string(kIntentRelative),
                        "attempt 的输入摘要与当前选择一致",
                        "selection_digest 已变（当前 " + selection_digest->to_hex() + "，attempt "
                            + intent.selection_digest + ")",
                        std::string(kIntentRelative),
                        "先运行 recover " + request.project_name
                            + " --action restore 撤销本次改动，再按当前选择运行 update"));
                    return;
                }
            }

            // 6. 内容项是否已全部落盘：成立才允许"只走提交点"的续做
            const CompletionCheck check = has_only_completed_content(result.project_root, intent, writer);
            readings.is_content_complete = check.is_complete;
            if (!check.is_ok)
            {
                result.diagnostics.add(make_diagnostic(
                    ProjectCategory::RecoveryConflict, std::string(kIntentRelative),
                    "能读取 attempt 涉及的全部内容项", "读取内容项失败或标记不成对", std::string(kIntentRelative),
                    "确认项目文件未被其它进程占用后重跑原命令；或 recover <name> --action restore 撤销本次改动"));
            }
        }

        // manifest 条目与磁盘一致（spec §8.3）：条目缺失 → record_invalid，摘要不符 → unmanaged_content
        void check_manifest_disk(const std::filesystem::path& project_root, const ManifestRecord& manifest,
                                 ID::CMake::FragmentWriter& writer, DiagnosticSet& diagnostics)
        {
            for (const ManifestEntry& entry : manifest.entries)
            {
                if (entry.kind == ManifestKind::Seed)
                {
                    continue;
                }

                const PathDigest current = current_object_digest(project_root, entry.path, entry.fragment, writer);
                if (!current.is_ok)
                {
                    diagnostics.add(make_diagnostic(ProjectCategory::RecordInvalid, entry.path,
                                                    "能读取该受管条目的当前内容", "属性查询或读取失败", entry.path,
                                                    "确认该文件未被其它进程占用或有读权限后重跑"));
                    continue;
                }
                if (!current.digest.has_value())
                {
                    diagnostics.add(make_diagnostic(
                        ProjectCategory::RecordInvalid, entry.path, "manifest 登记的文件存在",
                        "文件不存在（记录被外部改动）", entry.path,
                        "运行 update 从可信来源重建该工具文件；不要手工修改 manifest.json"));
                    continue;
                }
                if (current.digest->to_hex() != entry.digest)
                {
                    diagnostics.add(make_diagnostic(
                        ProjectCategory::UnmanagedContent, entry.path, entry.digest,
                        "当前摘要 " + current.digest->to_hex() + "（受管文件被本地修改）", entry.path,
                        "确认该修改是否要保留：要保留则移出生成范围，要放弃则运行 update --force"));
                }
            }
        }
    } // namespace

    OfflineSelection evaluate_offline_selection(const SelectionFile& selection, const ResolvedRecord& resolved)
    {
        OfflineSelection result;

        const PluginCatalog catalog = build_accepted_catalog(resolved);
        const EffectiveSelection expanded = expand_direct_selection(selection, catalog);
        if (!expanded.is_valid)
        {
            std::string actual;
            for (const ProjectDiagnostic& diagnostic : expanded.diagnostics.span())
            {
                if (is_warning(diagnostic.category))
                {
                    continue;
                }
                if (!actual.empty())
                {
                    actual += "；";
                }
                actual += diagnostic.subject + "：" + diagnostic.actual;
            }
            result.diagnostic = make_diagnostic(
                ProjectCategory::StaleGenerated, std::string(kSelectionFile),
                "plugins.json 的有效选择与 resolved 的校验快照一致",
                actual.empty() ? std::string("直接选择无法按已接受的清单视图展开") : actual,
                std::string(kSelectionFile),
                "运行 update 按当前 plugins.json 与实际源清单重新组装");
            return result;
        }

        const Resolution resolution = resolve_closure(expanded, catalog);
        if (!resolution.is_valid)
        {
            std::string actual;
            for (const ProjectDiagnostic& diagnostic : resolution.diagnostics.span())
            {
                if (is_warning(diagnostic.category))
                {
                    continue;
                }
                if (!actual.empty())
                {
                    actual += "；";
                }
                actual += diagnostic.subject + "：" + diagnostic.actual;
            }
            result.diagnostic = make_diagnostic(
                ProjectCategory::StaleGenerated, std::string(kSelectionFile),
                "plugins.json 的有效选择与 resolved 的校验快照一致",
                actual.empty() ? std::string("依赖闭包无法按已接受的清单视图解析") : actual,
                std::string(kSelectionFile),
                "运行 update 按当前 plugins.json 与实际源清单重新组装");
            return result;
        }

        result.digest = compute_selection_digest(resolution.selection);
        result.is_ok  = true;

        if (result.digest.to_hex() != resolved.selection_digest)
        {
            std::string detail = describe_selection_difference(resolution.selection, resolved);
            if (detail.empty())
            {
                detail = "规范化有效选择摘要与快照不同";
            }
            result.diagnostic = make_diagnostic(
                ProjectCategory::StaleGenerated, std::string(kSelectionFile), resolved.selection_digest,
                detail + "（当前摘要 " + result.digest.to_hex() + "）", std::string(kSelectionFile),
                "运行 update 按当前 plugins.json 与实际源清单重新组装");
        }

        return result;
    }

    std::optional<ProjectDiagnostic> check_offline_selection(const SelectionFile& selection,
                                                             const ResolvedRecord& resolved)
    {
        return evaluate_offline_selection(selection, resolved).diagnostic;
    }

    PreflightResult run_preflight(const PreflightRequest& request)
    {
        PreflightResult result;
        result.command      = request.command;
        result.project_name = request.project_name;

        // 1. 项目名形态：它同时是 CMake 工程名与可执行目标名
        if (!check_project_name(request.project_name))
        {
            result.diagnostics.add(make_diagnostic(
                ProjectCategory::ProjectNameInvalid, request.project_name,
                "匹配 ^[A-Za-z][A-Za-z0-9_-]*$ 的项目名", request.project_name.empty() ? "空串" : request.project_name,
                std::string(kRegisterEntry), "改正 --name 或 project.json 里的 name 后重跑"));
            return result;
        }

        // 2. 登记：先取样再读；写回由执行阶段在登记锁内重读。
        //    `--check` 的受检目标从这里开始：`project_exists`、`project_not_found` 与 `is_initialized`
        //    的差异都由登记决定，取样晚于读取就证明不了读取时的状态（spec §16 第 17 条）
        if (request.is_check)
        {
            const std::vector<SnapshotTarget> registry_targets{registry_snapshot_target(request.registry_path)};
            append_snapshot(result.snapshot_targets, result.snapshot_before, registry_targets);
        }
        result.registry = load_registry(request.registry_path);
        if (!result.registry.is_valid)
        {
            append_diagnostics(result.diagnostics, result.registry.diagnostics);
            return result;
        }
        result.entry = find_by_name(result.registry, request.project_name);

        // 3. 项目根：new 取 --path，其它命令取登记
        const bool is_creation = request.command == CommandKind::New;
        PathCheck root_check;
        if (is_creation)
        {
            if (request.project_path_text.empty())
            {
                result.diagnostics.add(make_diagnostic(
                    ProjectCategory::ProjectPathInvalid, request.project_name, "一个绝对路径",
                    "--path 为空", std::string(kRegisterEntry), "给出绝对的 --path 后重跑（spec §2.1）"));
                return result;
            }
            root_check = normalize_absolute(request.project_path_text, false);
            if (root_check.verdict != PathVerdict::Ok)
            {
                result.diagnostics.add(make_diagnostic(
                    ProjectCategory::ProjectPathInvalid, request.project_path_text, "可规范化的绝对路径",
                    root_check.detail.empty() ? std::string("路径不合法") : root_check.detail,
                    std::string(kRegisterEntry), "把 --path 改成已存在的绝对路径后重跑"));
                return result;
            }
            // 统一到长名形态：短名与长名是同一目录，比较与登记都用展开后的形态（spec §2.1）
            root_check.text = expand_existing_prefixes(root_check.text);
            if (result.entry != nullptr && !is_same_absolute_path(result.entry->path, root_check.text))
            {
                result.diagnostics.add(make_diagnostic(
                    ProjectCategory::ProjectExists, request.project_name, result.entry->path, root_check.text,
                    std::string(kRegisterEntry),
                    "同名项目已登记在另一个路径：改名或改用已登记路径；--force 不放行"));
                return result;
            }
            const RegistryEntry* entry_by_path = find_by_path(result.registry, root_check.text);
            if (entry_by_path != nullptr && entry_by_path != result.entry)
            {
                result.diagnostics.add(make_diagnostic(ProjectCategory::ProjectExists, entry_by_path->name,
                                                       "该路径未被其它项目登记", root_check.text,
                                                       std::string(kRegisterEntry),
                                                       "该路径已登记给另一个项目：换路径或改用该项目名"));
                return result;
            }
        }
        else
        {
            if (result.entry == nullptr)
            {
                result.diagnostics.add(make_diagnostic(
                    ProjectCategory::ProjectNotFound, request.project_name, "project.json 中有一条同名登记",
                    "未找到同名登记", std::string(kRegisterEntry),
                    "先运行 ID project new 登记该路径，或改正 --name 后重跑"));
                return result;
            }
            root_check = normalize_absolute(result.entry->path, false);
            if (root_check.verdict != PathVerdict::Ok)
            {
                result.diagnostics.add(make_diagnostic(
                    ProjectCategory::ProjectPathInvalid, request.project_name, "可规范化的绝对路径",
                    root_check.detail.empty() ? result.entry->path : root_check.detail, std::string(kRegisterEntry),
                    "修正 project.json 里的 path 后重跑"));
                return result;
            }
            // 旧登记可能留着短名形态的 path：后续判定统一按展开后的形态进行
            root_check.text = expand_existing_prefixes(root_check.text);
        }

        const std::filesystem::path project_root = to_native(root_check.text);
        result.project_root = project_root;
        result.engine_root  = request.engine_root;

        // 4. 路径身份：与工程根重叠、落在工具子树内、卷根或主目录都不允许
        const std::filesystem::path id_path    = request.engine_root / "ID";
        const std::filesystem::path plugin_path = request.plugin_root;
        const std::filesystem::path bench_path = request.engine_root / "Benchmark";
        if (is_same_or_nested(project_root, request.engine_root) || is_same_or_nested(request.engine_root, project_root)
            || is_same_or_nested(id_path, project_root) || is_same_or_nested(plugin_path, project_root)
            || is_same_or_nested(bench_path, project_root))
        {
            result.diagnostics.add(make_diagnostic(
                ProjectCategory::ProjectPathOverlap, request.project_name,
                "与工程根、ID/IDPlugin/Benchmark 子树互不包含的项目根", root_check.text,
                std::string(kRegisterEntry),
                "把项目放到独立的绝对路径（不在工程根内、也不包含工程根）后重跑（spec §2.2）"));
            return result;
        }
        if (is_special_directory(project_root))
        {
            result.diagnostics.add(make_diagnostic(
                ProjectCategory::ProjectPathInvalid, request.project_name, "一个普通目录作为项目根",
                "卷根、用户主目录或系统目录", root_check.text, "换一个普通目录后重跑（spec §2.2）"));
            return result;
        }

        // 5. 目录状态
        bool is_query_ok = false;
        const bool is_existing = path_exists(project_root, is_query_ok);
        if (!is_query_ok)
        {
            result.diagnostics.add(make_diagnostic(ProjectCategory::ProjectPathInvalid, request.project_name,
                                                   "能查询该路径的属性", "属性查询失败", root_check.text,
                                                   "修正路径权限后重跑"));
            return result;
        }
        if (!is_existing && !is_creation)
        {
            result.diagnostics.add(make_diagnostic(
                ProjectCategory::ProjectPathInvalid, request.project_name, "已存在的项目目录", "目录不存在",
                root_check.text, "先运行 ID project new 创建项目，或修正 project.json 的 path"));
            return result;
        }
        if (is_existing)
        {
            bool is_type_ok = false;
            const bool is_dir = is_directory(project_root, is_type_ok);
            if (!is_type_ok || !is_dir)
            {
                result.diagnostics.add(make_diagnostic(ProjectCategory::ProjectPathInvalid, request.project_name,
                                                       "一个目录作为项目根", "目标不是目录", root_check.text,
                                                       "改用一个目录路径后重跑"));
                return result;
            }
            if (is_creation)
            {
                const DirectoryList listing = list_directory(project_root);
                if (!listing.is_ok)
                {
                    result.diagnostics.add(make_diagnostic(ProjectCategory::ProjectPathInvalid, request.project_name,
                                                           "能列出该目录", "目录不可读", root_check.text,
                                                           "修正目录权限后重跑"));
                    return result;
                }
                if (!listing.names.empty() && result.entry == nullptr
                    && !is_only_bootstrap_residue(project_root, listing))
                {
                    result.diagnostics.add(make_diagnostic(
                        ProjectCategory::ProjectExists, request.project_name, "空目录或已登记的同路径项目",
                        "目录非空且未登记", root_check.text,
                        "腾空该目录或换一个空目录；已登记项目请直接运行 update（--force 不放行）"));
                    return result;
                }
            }
        }

        // 6. `--check` 的快照续段：项目内状态文件在选择与记录被读取之前取样，接到第 2 步的登记目标之后；
        //    复验由编排层在计划产出后做（spec §16 第 17 条、§10.12）。
        //    真实执行路径不取样：那里有项目锁与锁内基线复验（spec §12.2）
        if (request.is_check)
        {
            const std::vector<SnapshotTarget> project_targets =
                check_snapshot_targets(project_root, request.command);
            append_snapshot(result.snapshot_targets, result.snapshot_before, project_targets);
        }

        // 7. 选择与插件发现：clean 不读 plugins.json 也不读清单；build/recover 不读源插件库（spec §8.3、§12.1）
        const bool needs_source_catalog =
            request.command == CommandKind::New || request.command == CommandKind::Update;
        if (request.command != CommandKind::Clean)
        {
            const std::filesystem::path selection_path = project_root / to_native(kSelectionFile);
            if (request.command == CommandKind::New && request.has_selection_override)
            {
                // 选择来自 CLI 展开：`is_present` 仍表示"磁盘上有 plugins.json"，首次写出由计划安排
                result.selection            = request.selection_override;
                result.selection.is_valid   = true;
                result.selection.is_present = false;
                result.has_selection        = true;
                if (result.selection.name.empty())
                {
                    result.selection.name = request.project_name;
                }
            }
            else
            {
                result.selection = load_selection(selection_path);
                if (!result.selection.is_present)
                {
                    // recover 没有 plugins.json 也能撤销：输入摘要不可证时由 §5.8 的判定给出类别，不在这里抢先拒绝
                    if (request.command != CommandKind::Recover)
                    {
                        result.diagnostics.add(make_diagnostic(
                            ProjectCategory::ProjectConfigMissing, std::string(kSelectionFile),
                            "项目根内的 plugins.json", "文件不存在", std::string(kSelectionFile),
                            is_creation ? "用 --plugin <spec> 或 --empty 给出首次选择后重跑"
                                        : "恢复 plugins.json 后重跑（它是唯一的选择意图源）"));
                    }
                }
                else if (!result.selection.is_valid)
                {
                    append_diagnostics(result.diagnostics, result.selection.diagnostics);
                }
                else
                {
                    const FileRead raw = read_file(selection_path);
                    if (raw.status == ReadStatus::Found)
                    {
                        result.selection_bytes_digest = digest_text(raw.bytes).to_hex();
                    }
                    else
                    {
                        result.diagnostics.add(make_diagnostic(
                            ProjectCategory::ProjectConfigInvalid, std::string(kSelectionFile), "能读取该文件",
                            "读取失败", std::string(kSelectionFile), "确认文件未被占用或有读权限后重跑"));
                    }

                    // recover 是跨命令收敛入口：输出来源已坏时只妨碍续做，不阻止 restore
                    if (request.command != CommandKind::Recover)
                    {
                        if (const std::optional<ProjectDiagnostic> mismatch =
                                check_selection_name(result.selection, request.project_name))
                        {
                            result.diagnostics.add(*mismatch);
                        }
                    }
                    result.has_selection = true;
                }
            }

            // 源插件库只由 new/update 读取：build 与 recover 的判定只用 resolved 的校验快照（spec §8.3、§12.1）
            if (needs_source_catalog)
            {
                result.catalog = discover_plugins(request.plugin_root);
                if (!result.catalog.is_ok)
                {
                    result.diagnostics.add(make_diagnostic(
                        ProjectCategory::ProjectPathInvalid, to_utf8(request.plugin_root), "可读的插件目录",
                        "目录不存在或不可读", to_utf8(request.plugin_root),
                        "确认工程根的 IDPlugin 目录存在且可读后重跑"));
                }
                append_diagnostics(result.diagnostics, result.catalog.diagnostics);
            }
        }

        // 8. 记录：两份记录必须同时存在、逐项一致；intent 存在时按 U5 §5.8 判窗口
        const std::filesystem::path resolved_path = record_path(project_root, kResolvedRelative);
        const std::filesystem::path manifest_path = record_path(project_root, kManifestRelative);
        const std::filesystem::path intent_path   = record_path(project_root, kIntentRelative);

        const FileRead resolved_read = read_file(resolved_path);
        const FileRead manifest_read = read_file(manifest_path);
        const FileRead intent_read   = read_file(intent_path);

        if (resolved_read.status == ReadStatus::Failed || manifest_read.status == ReadStatus::Failed
            || intent_read.status == ReadStatus::Failed)
        {
            result.diagnostics.add(make_diagnostic(
                ProjectCategory::RecordInvalid, std::string(kResolvedRelative), "能读取记录文件", "读取失败",
                std::string(kResolvedRelative), "确认记录文件未被其它进程占用或有读权限后重跑"));
            return result;
        }

        RecordReadings& readings = result.records;
        readings.has_resolved = resolved_read.status == ReadStatus::Found;
        readings.has_manifest = manifest_read.status == ReadStatus::Found;
        readings.has_intent   = intent_read.status == ReadStatus::Found;

        std::string resolved_digest_hex;
        std::string manifest_digest_hex;
        if (readings.has_resolved)
        {
            resolved_digest_hex      = digest_text(resolved_read.bytes).to_hex();
            readings.resolved_bytes  = resolved_read.bytes;
        }
        if (readings.has_manifest)
        {
            manifest_digest_hex = digest_text(manifest_read.bytes).to_hex();
            readings.manifest_bytes = manifest_read.bytes;
        }

        if (readings.has_resolved != readings.has_manifest)
        {
            result.diagnostics.add(make_diagnostic(
                ProjectCategory::RecordInvalid, readings.has_resolved ? std::string(kManifestRelative)
                                                                      : std::string(kResolvedRelative),
                "resolved.json 与 manifest.json 同时存在", "只有其中一份存在",
                readings.has_resolved ? std::string(kManifestRelative) : std::string(kResolvedRelative),
                "删除残留的一份记录并重跑 update；或用 recover 收敛未完成的事务"));
        }
        else if (readings.has_resolved)
        {
            bool is_consistent = true;
            const RecordRead<ResolvedRecord> parsed_resolved = parse_resolved(resolved_read.bytes);
            if (!parsed_resolved.is_ok)
            {
                is_consistent = false;
                if (parsed_resolved.diagnostic.has_value())
                {
                    result.diagnostics.add(*parsed_resolved.diagnostic);
                }
            }
            const RecordRead<ManifestRecord> parsed_manifest = parse_manifest_record(manifest_read.bytes);

            // 安全类校验先于摘要自洽：条目路径的形态违规按 path_outside_project 报出，
            // 不被 entries_digest 的错配掩盖成 record_invalid（spec §13.2）
            const bool has_illegal_path = report_illegal_manifest_paths(parsed_manifest.value, result.diagnostics);
            if (!parsed_manifest.is_ok)
            {
                is_consistent = false;
                if (!has_illegal_path && parsed_manifest.diagnostic.has_value())
                {
                    result.diagnostics.add(*parsed_manifest.diagnostic);
                }
            }
            if (is_consistent)
            {
                readings.resolved = parsed_resolved.value;
                readings.manifest = parsed_manifest.value;
                if (const std::optional<ProjectDiagnostic> mismatch =
                        check_records(readings.resolved, readings.manifest, manifest_read.bytes))
                {
                    is_consistent = false;
                    // 有 intent 时先走 §5.8：generation/manifest_digest 层面的错配可能正是"提交未完成窗口"，
                    // 由 judge_intent 用 base 与目标快照证明；只有不可能由该窗口解释的错配才就地报 record_invalid
                    if (!readings.has_intent
                        || !is_pending_commit_shape(readings.resolved, readings.manifest, manifest_read.bytes))
                    {
                        result.diagnostics.add(*mismatch);
                    }
                }
            }
            readings.is_trusted = is_consistent;
        }

        // 9. intent 的收敛判定（判定顺序见 U5 §5.8）
        ID::CMake::FragmentWriter writer(to_utf8(project_root));
        if (readings.has_intent)
        {
            const RecordRead<IntentRecord> parsed_intent = parse_intent(intent_read.bytes);
            if (!parsed_intent.is_ok)
            {
                if (parsed_intent.diagnostic.has_value())
                {
                    result.diagnostics.add(*parsed_intent.diagnostic);
                }
            }
            else
            {
                readings.intent = parsed_intent.value;
                judge_intent(request, resolved_digest_hex, manifest_digest_hex, result, writer);
            }
        }

        // 10. build 的专属复验：记录可信、条目与磁盘一致、选择与校验快照一致
        if (request.command == CommandKind::Build)
        {
            if (!readings.is_trusted)
            {
                // 类别只看"有没有记录"：两份记录都不存在才是没有已提交组装（project_not_initialized，1）；
                // 有记录但不自洽/互不一致（缺一份、generation 或摘要不符、解析失败）已在第 7 步按
                // record_invalid（2）报告，这里不再降级成"未初始化"。
                // 有未完成的 intent 时类别已由 §5.8 给出（recovery_conflict，2），同样不降级。
                const bool has_any_record = readings.has_resolved || readings.has_manifest;
                if (!readings.has_intent && !has_any_record)
                {
                    result.diagnostics.add(make_diagnostic(
                        ProjectCategory::ProjectNotInitialized, request.project_name, "已有一次成功提交的组装",
                        "没有可信的已提交记录", std::string(kResolvedRelative),
                        "先运行 ID project new 或 update 提交一次组装，再构建"));
                }
            }
            else
            {
                check_manifest_disk(project_root, readings.manifest, writer, result.diagnostics);
                if (const std::optional<ProjectDiagnostic> stale =
                        check_offline_selection(result.selection, readings.resolved))
                {
                    result.diagnostics.add(*stale);
                }
            }
        }

        // 11. 已提交项目的 new：选择或受管树已变时只报 stale_generated，不组装、不覆盖 plugins.json
        if (is_creation && readings.is_trusted && result.has_selection)
        {
            check_manifest_disk(project_root, readings.manifest, writer, result.diagnostics);
            if (const std::optional<ProjectDiagnostic> stale =
                    check_offline_selection(result.selection, readings.resolved))
            {
                result.diagnostics.add(*stale);
            }
        }

        // 12. 派生缓存：is_initialized 与已提交记录不符时只报 warn，不改退出码。
        //     由 intent 快照可证的窗口里记录错配已被解释，标记仍是上一代的真值，不算待收尾。
        //     当前 `resolved` 等于 attempt 的 base 快照时同样成立：上一代提交仍然有效，与本次命令是否
        //     接管该 attempt 无关（例如命令不匹配被拒时，标记不是错的）
        const bool is_base_generation =
            readings.has_intent && matches_snapshot(readings.intent.base_resolved, readings.has_resolved,
                                                    resolved_digest_hex);
        const bool is_committed = readings.is_trusted || readings.is_intent_window || is_base_generation;
        if (result.entry != nullptr && result.entry->is_initialized != is_committed)
        {
            result.diagnostics.add(make_diagnostic(
                ProjectCategory::RegistryMarkerStale, request.project_name,
                std::string("is_initialized 与已提交记录一致"),
                result.entry->is_initialized ? "标记为 true 但没有可信的已提交记录" : "标记为 false 但记录已提交",
                std::string(kRegisterEntry),
                "运行 update 重写该派生缓存；它不改变记录的可信性判定"));
        }

        result.is_ok = !result.diagnostics.has_error();
        return result;
    }
} // namespace ID::Project
