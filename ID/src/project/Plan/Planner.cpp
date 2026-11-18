#include "project/Plan/Planner.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cmake/File/FragmentLayout.hpp"
#include "cmake/File/FragmentWriter.hpp"
#include "project/Base/Digest.hpp"
#include "project/Base/PathText.hpp"
#include "project/Base/Text.hpp"
#include "project/Emit/GeneratedTexts.hpp"
#include "project/Input/Linkage.hpp"
#include "project/Input/Selection.hpp"
#include "project/Plan/Ownership.hpp"
#include "project/Plan/PathDigest.hpp"
#include "project/Plan/Retention.hpp"
#include "project/Record/RecordJson.hpp"
#include "project/Record/RecordPaths.hpp"
#include "project/Resolve/Generation.hpp"

/**
 *  P1 的八步实现与动作展开。
 *
 *  顺序不可颠倒：`generation_id` 不依赖任何记录字节，也不依赖计划摘要；而 `manifest_digest` 依赖 manifest
 *  的最终文本，因此必须先定身份、再构造记录，计划摘要最后算。
 *
 *  片段的 D/O/N 一律取片段文本范围：D、O 来自记录与当前整文件快照，N 来自"组合后的目标全文"的同一套标记解析，
 *  不自行拼接片段文本再哈希，也不用整文件摘要顶替。
 */

namespace ID::Project
{
    namespace
    {
        constexpr std::string_view kFragmentOutputDirs    = "output-dirs";
        constexpr std::string_view kFragmentPluginTargets = "plugin-targets";
        constexpr std::string_view kSelectionFile         = "plugins.json";
        constexpr std::string_view kRegisterRelative      = "ID/project.json";
        constexpr std::string_view kWritePrefix           = "write:";
        constexpr std::string_view kPayloadPrefix         = "payload:";
        constexpr std::string_view kBackupPrefix          = "backup:";
        constexpr std::string_view kDeletePrefix          = "delete:";
        constexpr std::string_view kMkdirPrefix           = "mkdir:";
        constexpr std::string_view kRegisterPrefix        = "register:";

        DigestState state_of(const std::optional<Digest>& digest)
        {
            DigestState state;
            if (digest.has_value())
            {
                state.is_absent = false;
                state.hex       = digest->to_hex();
            }
            else
            {
                state.is_absent = true;
            }
            return state;
        }

        std::optional<std::string_view> fragment_view(const std::optional<std::string>& fragment)
        {
            if (!fragment.has_value())
            {
                return std::nullopt;
            }
            return std::string_view(*fragment);
        }

        std::string make_identity_id(std::string_view prefix, std::string_view path,
                                     const std::optional<std::string>& fragment)
        {
            std::string id(prefix);
            id += path;
            if (fragment.has_value())
            {
                id += '#';
                id += *fragment;
            }
            return id;
        }

        bool is_before_identity(std::string_view left_path, const std::optional<std::string>& left_fragment,
                                std::string_view right_path, const std::optional<std::string>& right_fragment) noexcept
        {
            if (left_path != right_path)
            {
                return left_path < right_path;
            }
            if (left_fragment.has_value() != right_fragment.has_value())
            {
                return !left_fragment.has_value();
            }
            return !left_fragment.has_value() || std::string_view(*left_fragment) < std::string_view(*right_fragment);
        }

        /**
         *  计划里的一个内容项：身份、目标摘要 N、当前摘要 D 与是否需要写内容/备份。
         */
        struct ContentItem
        {
            std::string                path           = {};
            std::optional<std::string> fragment       = {};
            Digest                     planned        = {};       // N
            DigestState                before         = {};       // D
            bool                       is_need_write  = false;
            bool                       is_need_backup = false;
            bool                       is_copy        = false;
            std::string                source         = {};       // 复制来源绝对路径
            std::string                bytes          = {};       // 写入内容（生成项或片段体）
            PayloadType                payload_type   = PayloadType::File;
        };

        std::optional<Digest> find_manifest_digest(const std::vector<ManifestEntry>& entries, std::string_view path,
                                                   const std::optional<std::string>& fragment)
        {
            for (const ManifestEntry& entry : entries)
            {
                if (entry.path != path)
                {
                    continue;
                }
                if (entry.fragment.has_value() != fragment.has_value())
                {
                    continue;
                }
                if (fragment.has_value() && *entry.fragment != *fragment)
                {
                    continue;
                }
                Digest parsed;
                if (!digest_from_hex(entry.digest, parsed))
                {
                    return std::nullopt;
                }
                return parsed;
            }
            return std::nullopt;
        }

        /**
         *  续做窗口的归属证据：本 attempt 已写出、尚未提交的 `(path, fragment)` 由 `intent` 的操作项证明
         *  （spec §1.3 首行「`D = N` 且归属可证」、U5 §5.8「原命令按三态续做」）。
         *  只命中本 attempt 自己的写入项，不放宽"`O = null` 且无 intent 证据"的未登记判定。
         */
        const IntentOperation* find_intent_write(const IntentRecord& intent, std::string_view path,
                                                 const std::optional<std::string>& fragment) noexcept
        {
            for (const IntentOperation& operation : intent.operations)
            {
                if (operation.kind != OperationKind::Write)
                {
                    continue;
                }
                if (operation.path != path)
                {
                    continue;
                }
                if (operation.fragment.has_value() != fragment.has_value())
                {
                    continue;
                }
                if (fragment.has_value() && *operation.fragment != *fragment)
                {
                    continue;
                }
                return &operation;
            }
            return nullptr;
        }

        // 本次命令能否接手该 attempt：`recover` 是唯一的跨命令收敛入口（U5 §5.8），其余必须同命令
        bool can_adopt_intent(CommandKind command, IntentCommand intent_command) noexcept
        {
            if (command == CommandKind::Recover)
            {
                return true;
            }
            switch (command)
            {
                case CommandKind::New:   return intent_command == IntentCommand::New;
                case CommandKind::Clean: return intent_command == IntentCommand::Clean;
                default:                 return intent_command == IntentCommand::Update;
            }
        }

        /**
         *  续做时的动作结论：按 `intent` 操作项的 before / after 判三态。
         */
        enum class ContinuationAction : std::uint8_t
        {
            Rewrite  = 0,   // 当前等于 before：按正常写入重做该项
            Skip     = 1,   // 当前等于 after：内容已落盘，只更新记录
            Conflict = 2,   // 第三态：归属本 attempt 的内容已被外部改写
        };

        ContinuationAction judge_continuation(const std::optional<Digest>& disk, const IntentOperation& operation)
        {
            const ThreeState state = judge_three_state(disk, operation.before, operation.after);
            if (state == ThreeState::After)
            {
                return ContinuationAction::Skip;
            }
            if (state == ThreeState::Before)
            {
                return ContinuationAction::Rewrite;
            }
            return ContinuationAction::Conflict;
        }

        std::string identity_text(std::string_view path, const std::optional<std::string>& fragment)
        {
            std::string text(path);
            if (fragment.has_value())
            {
                text += '#';
                text += *fragment;
            }
            return text;
        }

        bool is_cleanup_category(std::string_view category) noexcept
        {
            return category == "cleanup_blocked";
        }

        /**
         *  记一条冲突。冲突只在两类集合里表达，因此这里同时给出可判定的诊断：类别、对象（`path#fragment`）、
         *  期望与实际、下一步动作。**只写集合不写诊断会让退出码停在 0**，等于把冲突伪装成成功。
         */
        void add_conflict(ProjectPlan& plan, std::vector<PlanConflict>& conflicts, std::string path,
                          const std::optional<std::string>& fragment, std::string_view category, std::string detail)
        {
            const bool is_cleanup = is_cleanup_category(category);
            const std::string identity = identity_text(path, fragment);

            ProjectDiagnostic diagnostic;
            diagnostic.category = is_cleanup ? ProjectCategory::CleanupBlocked : ProjectCategory::UnmanagedContent;
            diagnostic.subject  = identity;
            diagnostic.expected = is_cleanup ? "待删范围与保留集内的条目都由 manifest 证明归属且摘要一致"
                                             : "受管文件的当前内容可由 manifest 或本次计划内容证明";
            diagnostic.actual      = detail;
            diagnostic.location    = identity;
            diagnostic.next_action = is_cleanup
                                         ? "该路径只能由用户处置：要保留则移出 IDPlugin/**，要删除则手工删除后重跑；"
                                           "--force 只豁免摘要一致，不构成归属证明"
                                         : "确认本地改动是否要保留：要放弃则先备份再覆盖（update --force / clean --force），"
                                           "未登记路径任何 --force 都不接管";
            plan.diagnostics.add(diagnostic);

            PlanConflict conflict;
            conflict.path     = std::move(path);
            conflict.fragment = fragment;
            conflict.category = std::string(category);
            conflict.detail   = std::move(detail);
            conflicts.push_back(std::move(conflict));
        }

        ProjectDiagnostic make_diagnostic(ProjectCategory category, std::string subject, std::string expected,
                                          std::string actual, std::string location, std::string next_action)
        {
            return ProjectDiagnostic{category, std::move(subject), std::move(expected), std::move(actual),
                                     std::move(location), std::move(next_action)};
        }

        std::string describe_verdict(OwnershipVerdict verdict)
        {
            switch (verdict)
            {
                case OwnershipVerdict::BlockUnregistered:
                    return "未登记路径，没有可信记录证明归属";
                case OwnershipVerdict::BlockModifiedLocal:
                    return "受管文件被本地修改（当前摘要既不等于记录，也不等于本次内容）";
                default:
                    return "摘要关系不满足可覆盖条件";
            }
        }

        // 续做的第三态：归属本 attempt 的内容已被外部改写，续做与 `restore` 都不成立（U5 §5.8）
        void add_continuation_conflict(ProjectPlan& plan, std::string_view path,
                                       const std::optional<std::string>& fragment,
                                       const std::optional<Digest>& disk, const IntentOperation& operation)
        {
            const std::string identity = identity_text(path, fragment);
            std::string       actual   = "当前摘要 ";
            actual += disk.has_value() ? disk->to_hex() : std::string("absent");
            actual += " 既不等于该 attempt 的 before ";
            actual += operation.before.is_absent ? std::string("absent") : operation.before.hex;
            actual += "，也不等于 after ";
            actual += operation.after.is_absent ? std::string("absent") : operation.after.hex;

            plan.diagnostics.add(make_diagnostic(
                ProjectCategory::RecoveryConflict, identity,
                "本 attempt 已写出的内容仍在磁盘上（当前等于 before 或 after）", std::move(actual), identity,
                "运行 recover <name> --action restore 撤销本次改动，或按当前内容重新运行 update"));
            plan.is_valid = false;
        }

        // 目标路径的安全判定：祖先链上任一段命中重解析点即拒绝（spec §2.3）。
        // 先于归属与摘要判定执行——越界不是"内容冲突"，任何 `--force` 都不放行。
        bool is_target_inside_project(const std::filesystem::path& project_root, std::string_view relative,
                                      ProjectPlan& plan)
        {
            const std::filesystem::path absolute = project_root / to_native(relative);
            const PathCheck             check    = check_reparse_points_under(absolute, project_root);
            if (check.verdict == PathVerdict::Ok)
            {
                return true;
            }

            const bool is_link = check.verdict == PathVerdict::ReparsePoint;
            plan.diagnostics.add(make_diagnostic(
                ProjectCategory::PathOutsideProject, std::string(relative),
                "目标的祖先链不含符号链接、junction 或挂载点",
                is_link ? check.detail : "路径段属性查询失败：" + to_utf8(absolute), std::string(relative),
                "把该路径上的链接换成项目内的真实目录后重跑；工具不跟随链接读写项目外（spec §2.3）"));
            plan.is_valid = false;
            return false;
        }

        // seed 的摘要取首次创建时的文本摘要：已存在时沿用旧 manifest 记录，不重算磁盘内容
        std::string seed_digest_of(const std::vector<ManifestEntry>& entries, const GeneratedText& seed)
        {
            for (const ManifestEntry& entry : entries)
            {
                if (entry.kind == ManifestKind::Seed && entry.path == kSeedRelative)
                {
                    return entry.digest;
                }
            }
            return digest_text(seed.text).to_hex();
        }

        SelectionPlugin make_selection_plugin(const EffectivePlugin& plugin)
        {
            SelectionPlugin item;
            item.name    = plugin.name;
            item.linkage = plugin.linkage;
            for (const EffectiveOption& option : plugin.options)
            {
                OptionAssignment assignment;
                assignment.name  = option.name;
                assignment.value = option.value ? "ON" : "OFF";
                item.options.push_back(std::move(assignment));
            }
            return item;
        }

        // plugins.json 首次写出：直接选择项按拓扑序排列（同序即 serialize_selection 的稳定输入）
        std::vector<SelectionPlugin> build_selection_plugins(const EffectiveSelection& selection,
                                                             const std::vector<std::string>& init_order)
        {
            std::vector<SelectionPlugin> plugins;
            plugins.reserve(selection.direct.size());

            for (const std::string& name : init_order)
            {
                const auto found = std::find_if(selection.direct.begin(), selection.direct.end(),
                                                [&name](const EffectivePlugin& plugin) noexcept
                                                {
                                                    return plugin.name == name;
                                                });
                if (found != selection.direct.end())
                {
                    plugins.push_back(make_selection_plugin(*found));
                }
            }
            for (const EffectivePlugin& plugin : selection.direct)
            {
                const bool is_present = std::any_of(plugins.begin(), plugins.end(),
                                                    [&plugin](const SelectionPlugin& item) noexcept
                                                    {
                                                        return item.name == plugin.name;
                                                    });
                if (!is_present)
                {
                    plugins.push_back(make_selection_plugin(plugin));
                }
            }
            return plugins;
        }

        std::string build_generation_id(const ResolvedAssembly& assembly, const PluginCatalog& catalog)
        {
            GenerationContent content;
            content.rules_version    = std::string(kRulesVersion);
            content.project_name     = assembly.resolution.selection.project_name;
            content.selection_digest = compute_selection_digest(assembly.resolution.selection);

            for (const std::string& plugin : assembly.resolution.init_order)
            {
                const CatalogEntry* entry = find_plugin(catalog, plugin);
                if (entry == nullptr)
                {
                    continue;
                }
                Digest digest;
                if (!digest_from_hex(entry->manifest.manifest_digest, digest))
                {
                    continue;
                }
                content.manifest_digests.emplace_back(plugin, digest);
            }

            for (const AssetFile& file : assembly.assets.files)
            {
                ContentDigest delivered;
                delivered.path   = file.target_relative;
                delivered.digest = file.digest;
                content.delivered.push_back(std::move(delivered));
            }

            return compute_generation_id(content);
        }

        // 写入目标所需的缺失父目录（相对项目根）
        void collect_missing_directories(const std::filesystem::path& project_root, std::string_view path,
                                         std::vector<std::string>& directories)
        {
            std::string_view parent = parent_relative(path);
            while (!parent.empty())
            {
                bool is_query_ok = false;
                const bool is_existing = path_exists(project_root / to_native(parent), is_query_ok);
                if (!is_query_ok)
                {
                    return;
                }
                if (!is_existing)
                {
                    directories.emplace_back(parent);
                }
                parent = parent_relative(parent);
            }
        }

        std::size_t segment_depth(const std::string& value) noexcept
        {
            return static_cast<std::size_t>(std::count(value.begin(), value.end(), '/'));
        }

        ManifestEntry make_asset_entry(const AssetFile& asset, const std::string& generation_id)
        {
            ManifestEntry entry;
            entry.path          = asset.target_relative;
            entry.fragment      = std::nullopt;
            entry.kind          = asset.stage == AssetStage::Generate ? ManifestKind::Generated : ManifestKind::Copied;
            entry.plugin        = asset.plugin;
            entry.source        = asset.source_relative;
            entry.digest        = asset.digest.to_hex();
            entry.stage         = asset.stage == AssetStage::Generate ? ManifestStage::Generate : ManifestStage::Copy;
            entry.generation_id = generation_id;
            return entry;
        }

        ManifestEntry make_fragment_entry(std::string_view fragment, std::string digest,
                                         const std::string& generation_id)
        {
            ManifestEntry entry;
            entry.path          = std::string(kRootCmake);
            entry.fragment      = std::string(fragment);
            entry.kind          = ManifestKind::Generated;
            entry.plugin        = std::nullopt;
            entry.source        = std::nullopt;
            entry.digest        = std::move(digest);
            entry.stage         = ManifestStage::Generate;
            entry.generation_id = generation_id;
            return entry;
        }

        // 生成项的正文：整文件生成项的内容只在 emit_all 的产出里，资产计划只带摘要
        std::string find_generated_text(const GeneratedTexts& texts, std::string_view target)
        {
            if (texts.aggregate.target_relative == target)
            {
                return texts.aggregate.text;
            }
            for (const GeneratedText& plugin : texts.plugins)
            {
                if (plugin.target_relative == target)
                {
                    return plugin.text;
                }
            }
            if (texts.seed.target_relative == target)
            {
                return texts.seed.text;
            }
            return std::string();
        }

        std::string find_range_digest(const ID::CMake::FragmentLayout& layout, std::string_view file_bytes,
                                      std::string_view name)
        {
            for (const ID::CMake::FragmentRange& range : layout.ranges)
            {
                if (range.name == name)
                {
                    return ID::CMake::fragment_range_digest(file_bytes, range);
                }
            }
            return std::string();
        }

        // 条目是否是本次的待删项：待删项的摘要关系由 `plan_trim` 判定，不在这里重复报冲突
        bool is_deletion_candidate(const TrimPlan& trim, const ManifestEntry& entry)
        {
            for (const DeletionCandidate& candidate : trim.candidates)
            {
                if (candidate.entry.path != entry.path)
                {
                    continue;
                }
                if (candidate.entry.fragment.has_value() != entry.fragment.has_value())
                {
                    continue;
                }
                if (!entry.fragment.has_value() || *candidate.entry.fragment == *entry.fragment)
                {
                    return true;
                }
            }
            return false;
        }

        // 一个 manifest 条目的当前摘要：整文件条目取整文件字节，片段条目取片段文本范围。
        // 读取失败、类型不符或片段标记不成对时 `is_ok` 为假
        PathDigest probe_entry_digest(const std::filesystem::path& project_root, const ManifestEntry& entry,
                                      ID::CMake::FragmentWriter& writer)
        {
            if (!entry.fragment.has_value())
            {
                return probe_file_digest(project_root / to_native(entry.path));
            }

            PathDigest result;
            const std::string_view name = *entry.fragment;
            const ID::CMake::FragmentSnapshot snapshot = writer.read_fragments(entry.path, {&name, 1});
            if (snapshot.diagnostic.has_value() || snapshot.state != ID::CMake::FragmentState::Ok)
            {
                result.is_ok = false;
                return result;
            }
            for (const ID::CMake::FragmentRange& range : snapshot.ranges)
            {
                if (range.name != name)
                {
                    continue;
                }
                const std::string hex = ID::CMake::fragment_range_digest(snapshot.file_bytes, range);
                Digest            parsed;
                if (hex.empty() || !digest_from_hex(hex, parsed))
                {
                    result.is_ok = false;
                    return result;
                }
                result.digest = parsed;
                return result;
            }
            return result;   // 目标文件存在但该片段缺失：D = absent
        }

        // 根文件目标全文：已存在时按标记组合，首次创建时组装整文件
        bool compose_root_target(const PreflightResult& preflight, const PlanOptions& options,
                                 const GeneratedTexts& texts, std::string& target_bytes, ProjectPlan& plan)
        {
            const std::string_view names[2] = {kFragmentOutputDirs, kFragmentPluginTargets};

            if (!options.has_root_cmake)
            {
                target_bytes = ID::CMake::compose_new_root_file(texts.root_header.text, texts.root_output_dirs.text,
                                                               texts.root_plugin_targets.text);
                if (target_bytes.empty())
                {
                    plan.diagnostics.add(make_diagnostic(ProjectCategory::WriteFailed, std::string(kRootCmake),
                                                         "根 CMakeLists.txt 的整文件文本", "组装结果为空",
                                                         std::string(kRootCmake),
                                                         "检查生成单元（U8）的根文件三段文本后重跑"));
                    return false;
                }
                return true;
            }

            ID::CMake::FragmentWriter writer(to_utf8(preflight.project_root));
            const ID::CMake::FragmentSnapshot snapshot = writer.read_fragments(std::string(kRootCmake), names);
            if (snapshot.diagnostic.has_value())
            {
                plan.diagnostics.add(make_diagnostic(
                    ProjectCategory::WriteFailed, std::string(kRootCmake), "能读取根 CMakeLists.txt",
                    snapshot.diagnostic->actual, std::string(kRootCmake),
                    "确认该文件未被其它进程占用或有读写权限后重跑"));
                return false;
            }
            if (snapshot.state != ID::CMake::FragmentState::Ok)
            {
                add_conflict(plan, plan.write_conflicts, std::string(kRootCmake), std::nullopt, "unmanaged_content",
                             "受控片段标记缺失或不成对：" + snapshot.detail);
                return false;
            }

            ID::CMake::FragmentLayout layout;
            layout.state  = snapshot.state;
            layout.detail = snapshot.detail;
            layout.ranges = snapshot.ranges;

            const std::pair<std::string_view, std::string_view> replacements[2] = {
                {kFragmentOutputDirs, texts.root_output_dirs.text},
                {kFragmentPluginTargets, texts.root_plugin_targets.text}};

            const std::optional<std::string> composed =
                ID::CMake::compose_fragments(snapshot.file_bytes, layout, replacements);
            if (!composed.has_value())
            {
                add_conflict(plan, plan.write_conflicts, std::string(kRootCmake), std::nullopt, "unmanaged_content",
                             "按标记组合目标全文失败（标记不成对或片段范围越界）");
                return false;
            }

            const ID::CMake::FragmentLayout target_layout = ID::CMake::parse_fragments(*composed, names);
            if (target_layout.state != ID::CMake::FragmentState::Ok)
            {
                add_conflict(plan, plan.write_conflicts, std::string(kRootCmake), std::nullopt, "unmanaged_content",
                             "写后全文无法按同一套标记规则解析：" + target_layout.detail);
                return false;
            }

            target_bytes = *composed;
            return true;
        }
    } // namespace

    ProjectPlan build_plan(const ResolvedAssembly& assembly, const PreflightResult& preflight,
                           const PlanOptions& options)
    {
        ProjectPlan plan;
        plan.command  = options.command;
        plan.is_valid = true;

        for (const ProjectDiagnostic& diagnostic : assembly.diagnostics.span())
        {
            plan.diagnostics.add(diagnostic);
        }
        if (!assembly.is_valid)
        {
            plan.is_valid = false;
            return plan;
        }

        const std::filesystem::path project_root = preflight.project_root;
        const std::string project_name           = assembly.resolution.selection.project_name;
        const bool is_clean                      = options.command == CommandKind::Clean;

        // 生成文本：受控片段的正文与 limitations 只在 emit_all 里产出
        GeneratedTexts texts;
        if (!is_clean)
        {
            texts = emit_all(assembly, project_name);
            if (!texts.is_valid)
            {
                for (const ProjectDiagnostic& diagnostic : texts.diagnostics.span())
                {
                    plan.diagnostics.add(diagnostic);
                }
                plan.is_valid = false;
                return plan;
            }
            for (const AssetFile& asset : assembly.assets.files)
            {
                if (asset.stage == AssetStage::Generate && asset.digest.is_zero())
                {
                    plan.diagnostics.add(make_diagnostic(
                        ProjectCategory::WriteFailed, asset.target_relative,
                        "生成项摘要已由生成单元回填（apply_generated_digests）",
                        "摘要为全零（未回填），generation 身份与 manifest 不可信", asset.target_relative,
                        "调用方先回填生成文本摘要再构造计划（U8 → P1）"));
                    plan.is_valid = false;
                }
            }
            if (!plan.is_valid)
            {
                return plan;
            }
        }

        // 1. 保留集
        const RetentionSet retention = is_clean ? retention_from_record(preflight.records.resolved)
                                                : build_retention(assembly.assets, assembly.resolution);

        const std::vector<ManifestEntry>& old_entries = preflight.records.manifest.entries;

        // 续做窗口的归属证据：本次命令接手的 attempt 已写出的内容项，由 `intent` 的操作项证明归属
        // （spec §1.3 首行、U5 §5.8）。没有 `intent` 或命令接不上时证据不存在，`O = null` 的未登记判定照旧不放行
        const IntentRecord* const intent =
            preflight.records.has_intent ? &preflight.records.intent : nullptr;
        const bool has_intent_evidence =
            intent != nullptr && can_adopt_intent(options.command, intent->command);

        // 2. 写入集合
        std::vector<ContentItem> items;
        if (!is_clean)
        {
            for (const AssetFile& asset : assembly.assets.files)
            {
                // 路径安全先于归属与摘要判定：越界目标不是"内容冲突"，也不因 `--force` 放行
                if (!is_target_inside_project(project_root, asset.target_relative, plan))
                {
                    continue;
                }

                const PathDigest disk = probe_file_digest(project_root / to_native(asset.target_relative));
                if (!disk.is_ok)
                {
                    plan.diagnostics.add(make_diagnostic(
                        ProjectCategory::PathOutsideProject, asset.target_relative,
                        "目标是一个可读写的普通文件或不存在", "目标存在但不是普通文件，或读取失败",
                        asset.target_relative, "移走该路径上的非文件对象后重跑（spec §2.3）"));
                    plan.is_valid = false;
                    continue;
                }

                OwnershipInput input;
                input.disk      = disk.digest;
                input.record    = find_manifest_digest(old_entries, asset.target_relative, std::nullopt);
                input.planned   = asset.digest;
                input.has_force = options.has_force;

                const OwnershipVerdict verdict = judge_ownership(input);

                ContentItem item;
                item.path     = asset.target_relative;
                item.fragment = std::nullopt;
                item.planned  = asset.digest;
                item.before   = state_of(disk.digest);
                item.is_copy  = asset.stage == AssetStage::Copy;
                if (item.is_copy && asset.source_relative.has_value())
                {
                    item.source = to_utf8(preflight.engine_root / to_native(*asset.source_relative));
                }
                if (!item.is_copy)
                {
                    item.bytes = find_generated_text(texts, asset.target_relative);
                    if (item.bytes.empty() || digest_text(item.bytes).to_hex() != asset.digest.to_hex())
                    {
                        plan.diagnostics.add(make_diagnostic(
                            ProjectCategory::WriteFailed, asset.target_relative,
                            "生成项的正文与已回填摘要一致", "生成文本缺失或摘要不符", asset.target_relative,
                            "检查生成单元（U8）的产出与摘要回填后重跑"));
                        plan.is_valid = false;
                        continue;
                    }
                }

                // 续做：本 attempt 已写出的该路径由 `intent` 的 before/after 判三态，不再用旧 manifest 的 O
                const IntentOperation* const evidence =
                    has_intent_evidence ? find_intent_write(*intent, asset.target_relative, std::nullopt) : nullptr;
                if (evidence != nullptr)
                {
                    switch (judge_continuation(disk.digest, *evidence))
                    {
                        case ContinuationAction::Skip:
                            break;      // 内容已等于 after：只更新记录
                        case ContinuationAction::Rewrite:
                            item.is_need_write = true;
                            break;
                        case ContinuationAction::Conflict:
                            add_continuation_conflict(plan, asset.target_relative, std::nullopt, disk.digest,
                                                      *evidence);
                            continue;
                    }

                    items.push_back(std::move(item));
                    continue;
                }

                switch (verdict)
                {
                    case OwnershipVerdict::CreateNew:
                    case OwnershipVerdict::RestoreMissing:
                    case OwnershipVerdict::UpgradeManaged:
                        item.is_need_write = true;
                        break;
                    case OwnershipVerdict::RefreshIdentical:
                    case OwnershipVerdict::NoChange:
                        break;
                    case OwnershipVerdict::OverwriteForced:
                        item.is_need_write  = true;
                        item.is_need_backup = true;
                        break;
                    case OwnershipVerdict::BlockModifiedLocal:
                    case OwnershipVerdict::BlockUnregistered:
                        add_conflict(plan, plan.write_conflicts, asset.target_relative, std::nullopt, "unmanaged_content",
                                     describe_verdict(verdict));
                        plan.is_valid = false;
                        continue;
                    default:
                        add_conflict(plan, plan.write_conflicts, asset.target_relative, std::nullopt, "unmanaged_content",
                                     "所有权判定结论不适用于写入方向（实现缺陷）");
                        plan.is_valid = false;
                        continue;
                }

                items.push_back(std::move(item));
            }
        }

        // 3. 根文件两个受控片段（首次创建时是整文件写出）
        std::string root_target_bytes;
        bool is_root_target_ready = false;
        if (!is_clean)
        {
            // 根文件本体也参与写入：祖先链先判安全，越界时不读也不写该文件
            is_root_target_ready = is_target_inside_project(project_root, kRootCmake, plan)
                                && compose_root_target(preflight, options, texts, root_target_bytes, plan);
            if (!is_root_target_ready)
            {
                plan.is_valid = false;
            }
            else if (!options.has_root_cmake)
            {
                ContentItem item;
                item.path          = std::string(kRootCmake);
                item.fragment      = std::nullopt;
                item.planned       = digest_text(root_target_bytes);
                item.before        = state_of(std::nullopt);
                item.is_need_write = true;
                item.bytes         = root_target_bytes;
                item.payload_type  = PayloadType::File;
                items.push_back(std::move(item));
            }
            else
            {
                const std::string_view names[2] = {kFragmentOutputDirs, kFragmentPluginTargets};
                ID::CMake::FragmentWriter writer(to_utf8(project_root));
                const ID::CMake::FragmentSnapshot snapshot = writer.read_fragments(std::string(kRootCmake), names);

                ID::CMake::FragmentLayout source_layout;
                source_layout.state  = snapshot.state;
                source_layout.detail = snapshot.detail;
                source_layout.ranges = snapshot.ranges;

                const ID::CMake::FragmentLayout target_layout =
                    ID::CMake::parse_fragments(root_target_bytes, names);

                const GeneratedText* bodies[2] = {&texts.root_output_dirs, &texts.root_plugin_targets};
                for (std::size_t index = 0; index < 2; ++index)
                {
                    const std::string_view name      = names[index];
                    const std::string before_hex     = find_range_digest(source_layout, snapshot.file_bytes, name);
                    const std::string after_hex      = find_range_digest(target_layout, root_target_bytes, name);
                    Digest            before_digest;
                    Digest            after_digest;
                    if (before_hex.empty() || !digest_from_hex(before_hex, before_digest) || after_hex.empty()
                        || !digest_from_hex(after_hex, after_digest))
                    {
                        plan.diagnostics.add(make_diagnostic(
                            ProjectCategory::UnmanagedContent, std::string(kRootCmake),
                            "两个受控片段的文本范围可解析", "无法计算片段文本范围摘要", std::string(kRootCmake),
                            "修正根 CMakeLists.txt 的受控标记后重跑"));
                        plan.is_valid = false;
                        continue;
                    }

                    OwnershipInput input;
                    input.disk      = before_digest;
                    input.record    = find_manifest_digest(old_entries, kRootCmake, std::string(name));
                    input.planned   = after_digest;
                    input.has_force = options.has_force;

                    const OwnershipVerdict verdict = judge_ownership(input);

                    ContentItem item;
                    item.path         = std::string(kRootCmake);
                    item.fragment     = std::string(name);
                    item.planned      = after_digest;
                    item.before       = state_of(before_digest);
                    item.payload_type = PayloadType::Fragment;
                    item.bytes        = bodies[index]->text;

                    // 续做：该片段若由本次 attempt 写出过，按 intent 的 before/after 判三态
                    const IntentOperation* const evidence =
                        has_intent_evidence ? find_intent_write(*intent, kRootCmake, item.fragment) : nullptr;
                    if (evidence != nullptr)
                    {
                        switch (judge_continuation(before_digest, *evidence))
                        {
                            case ContinuationAction::Skip:
                                break;      // 片段已等于 after：只更新记录
                            case ContinuationAction::Rewrite:
                                item.is_need_write = true;
                                break;
                            case ContinuationAction::Conflict:
                                add_continuation_conflict(plan, kRootCmake, item.fragment, before_digest, *evidence);
                                continue;
                        }

                        items.push_back(std::move(item));
                        continue;
                    }

                    switch (verdict)
                    {
                        case OwnershipVerdict::RefreshIdentical:
                        case OwnershipVerdict::NoChange:
                            break;
                        case OwnershipVerdict::CreateNew:
                        case OwnershipVerdict::RestoreMissing:
                        case OwnershipVerdict::UpgradeManaged:
                            item.is_need_write = true;
                            break;
                        case OwnershipVerdict::OverwriteForced:
                            item.is_need_write  = true;
                            item.is_need_backup = true;
                            break;
                        case OwnershipVerdict::BlockModifiedLocal:
                        case OwnershipVerdict::BlockUnregistered:
                            add_conflict(plan, plan.write_conflicts, std::string(kRootCmake), item.fragment,
                                         "unmanaged_content", describe_verdict(verdict));
                            plan.is_valid = false;
                            continue;
                        default:
                            add_conflict(plan, plan.write_conflicts, std::string(kRootCmake), item.fragment,
                                         "unmanaged_content", "所有权判定结论不适用于写入方向（实现缺陷）");
                            plan.is_valid = false;
                            continue;
                    }

                    items.push_back(std::move(item));
                }
            }
        }

        // 4. 裁剪
        TrimPlan                   trim;
        std::vector<ManifestEntry> retained_modified;   // clean：保留集内但摘要不符的条目（--force 时先备份）
        if (preflight.records.has_manifest)
        {
            trim = plan_trim(preflight.records.manifest, retention, project_root, options.has_force);
            for (const ProjectDiagnostic& diagnostic : trim.diagnostics.span())
            {
                plan.diagnostics.add(diagnostic);
            }
            for (const ManifestEntry& entry : trim.blocked_modified)
            {
                add_conflict(plan, plan.cleanup_conflicts, entry.path, entry.fragment, "cleanup_blocked",
                             "受管文件被本地修改且未给 --force");
            }
            // 未登记路径的类别按命令取：写入型命令是写入范围冲突（U5 §5.9 第一行），`clean` 是裁剪范围
            // 冲突（spec §8.8）。同一路径不两类重复报，零删除零提交由"计划无效即不执行"保证
            for (const std::string& path : trim.unregistered)
            {
                if (is_clean)
                {
                    add_conflict(plan, plan.cleanup_conflicts, path, std::nullopt, "cleanup_blocked",
                                 "待删范围内的未登记路径（--force 不放行）");
                }
                else
                {
                    add_conflict(plan, plan.write_conflicts, path, std::nullopt, "unmanaged_content",
                                 "写入范围内的未登记路径（--force 不放行）");
                }
            }
            if (trim.is_blocked || !trim.blocked_modified.empty() || !trim.is_valid)
            {
                plan.is_valid = false;
            }

            // `clean` 不写内容：保留集内的条目当前摘要与记录不符时同样是冲突（U5 §5.9 第四行）；
            // 给了 `--force` 则先备份再继续。缺失的条目不是本命令的对象，跳过
            if (is_clean)
            {
                ID::CMake::FragmentWriter writer(to_utf8(project_root));
                for (const ManifestEntry& entry : preflight.records.manifest.entries)
                {
                    if (entry.kind == ManifestKind::Seed || is_deletion_candidate(trim, entry))
                    {
                        continue;
                    }
                    const PathDigest disk = probe_entry_digest(project_root, entry, writer);
                    if (!disk.is_ok || !disk.digest.has_value() || disk.digest->to_hex() == entry.digest)
                    {
                        continue;
                    }
                    if (options.has_force)
                    {
                        retained_modified.push_back(entry);
                        continue;
                    }
                    add_conflict(plan, plan.cleanup_conflicts, entry.path, entry.fragment, "cleanup_blocked",
                                 "受管文件被本地修改且未给 --force（当前 " + disk.digest->to_hex() + "，记录 "
                                     + entry.digest + "）");
                }
            }
        }

        // 5. seed：文件不存在才建，不参与冲突判定
        std::string seed_digest;
        if (!is_clean)
        {
            seed_digest = seed_digest_of(old_entries, texts.seed);

            // 越界时不读写该路径：记录照旧登记首次创建事实，内容留给用户处置
            if (is_target_inside_project(project_root, kSeedRelative, plan))
            {
                const PathDigest seed_probe = probe_file_digest(project_root / to_native(kSeedRelative));
                if (!seed_probe.is_ok)
                {
                    plan.diagnostics.add(make_diagnostic(
                        ProjectCategory::PathOutsideProject, std::string(kSeedRelative), "目标是一个普通文件或不存在",
                        "读取失败或类型不符", std::string(kSeedRelative), "检查该路径后重跑"));
                    plan.is_valid = false;
                }
                else if (!seed_probe.digest.has_value())
                {
                    ContentItem item;
                    item.path          = std::string(kSeedRelative);
                    item.fragment      = std::nullopt;
                    item.planned       = digest_text(texts.seed.text);
                    item.before        = state_of(std::nullopt);
                    item.is_need_write = true;
                    item.bytes         = texts.seed.text;
                    item.payload_type  = PayloadType::File;
                    items.push_back(std::move(item));
                }
            }
        }

        // 首次写出 plugins.json 的字节：记录里的 selection_bytes_digest 与实际写出的字节必须是同一份
        std::string new_selection_bytes;
        if (!is_clean && options.command == CommandKind::New && !preflight.selection.is_present
            && preflight.has_selection)
        {
            new_selection_bytes = serialize_selection(project_name,
                                                      build_selection_plugins(assembly.resolution.selection,
                                                                             assembly.resolution.init_order));
        }

        // 6. 记录：先定身份，再构造 manifest，最后算 manifest_digest
        std::string generation_id;
        std::string selection_digest;
        if (is_clean)
        {
            generation_id    = preflight.records.resolved.generation_id;
            selection_digest = preflight.records.resolved.selection_digest;
        }
        else
        {
            generation_id = assembly.generation_id.empty() ? build_generation_id(assembly, preflight.catalog)
                                                           : assembly.generation_id;
            selection_digest = compute_selection_digest(assembly.resolution.selection).to_hex();
        }
        plan.generation_id    = generation_id;
        plan.selection_digest = selection_digest;

        ManifestRecord next_manifest;
        next_manifest.schema_version = std::string(kSchemaVersion);
        next_manifest.generation_id  = generation_id;

        if (is_clean)
        {
            for (const ManifestEntry& entry : preflight.records.manifest.entries)
            {
                const bool is_deleted = std::any_of(trim.candidates.begin(), trim.candidates.end(),
                                                    [&entry](const DeletionCandidate& candidate) noexcept
                                                    {
                                                        return candidate.entry.path == entry.path
                                                            && candidate.entry.fragment.has_value()
                                                                   == entry.fragment.has_value()
                                                            && (!entry.fragment.has_value()
                                                                || *candidate.entry.fragment == *entry.fragment);
                                                    });
                if (!is_deleted)
                {
                    next_manifest.entries.push_back(entry);
                }
            }
        }
        else
        {
            for (const AssetFile& asset : assembly.assets.files)
            {
                next_manifest.entries.push_back(make_asset_entry(asset, generation_id));
            }

            ManifestEntry seed_entry;
            seed_entry.path          = std::string(kSeedRelative);
            seed_entry.fragment      = std::nullopt;
            seed_entry.kind          = ManifestKind::Seed;
            seed_entry.plugin        = std::nullopt;
            seed_entry.source        = std::nullopt;
            seed_entry.digest        = seed_digest;
            seed_entry.stage         = ManifestStage::Generate;
            seed_entry.generation_id = generation_id;
            next_manifest.entries.push_back(std::move(seed_entry));

            if (is_root_target_ready)
            {
                const std::string_view names[2] = {kFragmentOutputDirs, kFragmentPluginTargets};
                const ID::CMake::FragmentLayout target_layout =
                    ID::CMake::parse_fragments(root_target_bytes, names);
                if (target_layout.state == ID::CMake::FragmentState::Ok)
                {
                    for (const std::string_view name : names)
                    {
                        const std::string digest = find_range_digest(target_layout, root_target_bytes, name);
                        next_manifest.entries.push_back(make_fragment_entry(name, digest, generation_id));
                    }
                }
            }
        }

        std::sort(next_manifest.entries.begin(), next_manifest.entries.end(),
                  [](const ManifestEntry& left, const ManifestEntry& right) noexcept
                  {
                      return is_before_identity(left.path, left.fragment, right.path, right.fragment);
                  });
        next_manifest.entry_count    = static_cast<std::uint64_t>(next_manifest.entries.size());
        next_manifest.entries_digest = compute_entries_digest(next_manifest.entries).to_hex();

        const std::string manifest_bytes = serialize_manifest(next_manifest);
        const std::string manifest_digest = digest_text(manifest_bytes).to_hex();

        ResolvedRecord next_resolved;
        if (is_clean)
        {
            next_resolved                 = preflight.records.resolved;
            next_resolved.manifest_digest = manifest_digest;
        }
        else
        {
            next_resolved.schema_version    = std::string(kSchemaVersion);
            next_resolved.rules_version     = std::string(kRulesVersion);
            next_resolved.generation_id     = generation_id;
            next_resolved.project_name      = project_name;
            next_resolved.manifest_digest   = manifest_digest;
            next_resolved.selection_digest  = selection_digest;
            next_resolved.selection_bytes_digest = new_selection_bytes.empty()
                                                       ? preflight.selection_bytes_digest
                                                       : digest_text(new_selection_bytes).to_hex();

            for (const std::string& plugin : assembly.resolution.init_order)
            {
                const CatalogEntry* entry = find_plugin(preflight.catalog, plugin);
                if (entry == nullptr)
                {
                    continue;
                }

                ResolvedManifestSnapshot snapshot;
                snapshot.plugin          = entry->manifest.name;
                snapshot.manifest_digest = entry->manifest.manifest_digest;
                snapshot.version.clear();
                snapshot.dependencies = entry->manifest.dependencies;
                if (entry->manifest.requirement != Requirement::Any)
                {
                    snapshot.requirements.emplace_back(requirement_text(entry->manifest.requirement));
                }
                for (const ManifestOption& option : entry->manifest.options)
                {
                    ResolvedOptionSnapshot option_snapshot;
                    option_snapshot.name          = option.name;
                    option_snapshot.type          = "bool";     // v1 只有 bool
                    option_snapshot.default_on    = option.default_on;
                    option_snapshot.description   = option.description;
                    option_snapshot.define        = option.define;
                    option_snapshot.links_when_on = option.links_when_on;
                    snapshot.options.push_back(std::move(option_snapshot));
                }
                std::sort(snapshot.options.begin(), snapshot.options.end(),
                          [](const ResolvedOptionSnapshot& left, const ResolvedOptionSnapshot& right) noexcept
                          {
                              return std::string_view(left.name) < std::string_view(right.name);
                          });
                next_resolved.snapshot.push_back(std::move(snapshot));
            }

            // 首次写出的 `plugins.json` 含直接项的显式 `linkage_type` 与全部选项取值（U10 §10.8）：
            // 本次记录里这些取值的来源必须按"显式"落盘，否则回读同一份 `plugins.json` 的下一代
            // 会写出不同的记录字节，使"内容无变化的 update"变成一次假的记录重写
            for (const EffectivePlugin& plugin : assembly.resolution.selection.all)
            {
                const bool is_source_written = !new_selection_bytes.empty() && plugin.is_direct;

                ResolvedSelectionEntry entry;
                entry.plugin  = plugin.name;
                entry.linkage = std::string(to_string(plugin.linkage));
                entry.linkage_source =
                    is_source_written || plugin.linkage_source == LinkageSource::Explicit ? "explicit" : "requirement";
                entry.is_direct = plugin.is_direct;
                for (const EffectiveOption& option : plugin.options)
                {
                    entry.option_names.emplace_back(option.name);
                    entry.option_values.emplace_back(option.value ? "ON" : "OFF");
                    entry.option_sources.emplace_back(is_source_written || option.source == OptionSource::Explicit
                                                          ? "explicit"
                                                          : "default");
                }
                next_resolved.selection.push_back(std::move(entry));
            }

            for (const LinkEdge& edge : assembly.resolution.edges)
            {
                ResolvedEdge resolved_edge;
                resolved_edge.from                = edge.from;
                resolved_edge.to                  = edge.to;
                resolved_edge.controlling_options = edge.controlling_options;
                next_resolved.edges.push_back(std::move(resolved_edge));
            }
            next_resolved.init_order = assembly.resolution.init_order;

            for (const PluginNaming& naming : assembly.naming.plugins)
            {
                ResolvedTargetEntry target;
                target.plugin          = naming.plugin;
                target.target          = naming.target;
                target.option_vars     = naming.option_vars;
                target.option_defines  = naming.option_defines;
                target.static_macro    = naming.static_macro;
                target.export_macro    = naming.export_macro;

                for (const EffectivePlugin& plugin : assembly.resolution.selection.all)
                {
                    if (plugin.name == naming.plugin)
                    {
                        target.target_type = plugin.linkage == Linkage::Dynamic ? "SHARED" : "STATIC";
                        break;
                    }
                }

                for (const PluginCompileFacts& facts : assembly.resolution.compile_facts)
                {
                    if (facts.plugin != naming.plugin)
                    {
                        continue;
                    }
                    target.source_dirs = facts.source_dirs;
                    for (const IncludeRoot& root : facts.include_roots)
                    {
                        ResolvedIncludeRoot include_root;
                        include_root.path      = root.path;
                        include_root.is_system = root.is_system;
                        target.include_roots.push_back(std::move(include_root));
                    }
                    for (const VendorLink& link : facts.vendor_links)
                    {
                        ResolvedVendorLink vendor;
                        vendor.dir    = link.dir;
                        vendor.target = link.target;
                        target.vendor_links.push_back(std::move(vendor));
                    }
                    break;
                }

                next_resolved.targets.push_back(std::move(target));
            }

            for (const AssetFile& asset : assembly.assets.files)
            {
                ResolvedAssetEntry entry;
                entry.path     = asset.target_relative;
                entry.fragment = std::nullopt;
                entry.source   = asset.source_relative;
                entry.digest   = asset.digest.to_hex();
                entry.stage    = asset.stage == AssetStage::Generate ? "generate" : "copy";
                entry.plugin   = asset.plugin;
                next_resolved.assets.push_back(std::move(entry));
            }

            for (const std::string& path : retention.paths())
            {
                next_resolved.retention.push_back(path);
            }

            for (const EmitLimitation& limitation : texts.limitations)
            {
                ResolvedLimitation resolved_limitation;
                resolved_limitation.code    = limitation.code;
                resolved_limitation.message = limitation.message;
                next_resolved.limitations.push_back(std::move(resolved_limitation));
            }
        }

        const std::string resolved_bytes = serialize_resolved(next_resolved);

        if (const std::optional<ProjectDiagnostic> invalid = check_manifest_self(next_manifest))
        {
            plan.diagnostics.add(*invalid);
            plan.is_valid = false;
        }
        if (const std::optional<ProjectDiagnostic> invalid = check_resolved_self(next_resolved))
        {
            plan.diagnostics.add(*invalid);
            plan.is_valid = false;
        }
        if (const std::optional<ProjectDiagnostic> invalid =
                check_records(next_resolved, next_manifest, manifest_bytes))
        {
            plan.diagnostics.add(*invalid);
            plan.is_valid = false;
        }

        plan.next_manifest = std::move(next_manifest);
        plan.next_resolved = std::move(next_resolved);

        // 7. 计划摘要：写入集合 + 删除集合，只进 intent 与 --check 差异说明
        std::vector<ContentDigest> writes;
        std::vector<ContentDigest> deletes;
        for (const ContentItem& item : items)
        {
            if (!item.is_need_write)
            {
                continue;
            }
            ContentDigest entry;
            entry.path     = item.path;
            entry.fragment = item.fragment;
            entry.digest   = item.planned;
            writes.push_back(std::move(entry));
        }
        for (const DeletionCandidate& candidate : trim.candidates)
        {
            ContentDigest entry;
            entry.path     = candidate.entry.path;
            entry.fragment = candidate.entry.fragment;
            deletes.push_back(std::move(entry));
        }
        plan.plan_digest         = compute_plan_digest(writes, deletes);
        plan.content_write_count = writes.size();
        plan.delete_count        = deletes.size();

        // 8. 动作序列：mkdir → 登记/首次选择 → payload → 备份 → 内容写入 → 裁剪 → 记录 → 收尾 → 标记
        std::vector<std::string> mkdirs;
        for (const ContentItem& item : items)
        {
            if (item.is_need_write)
            {
                collect_missing_directories(project_root, item.path, mkdirs);
            }
        }
        std::sort(mkdirs.begin(), mkdirs.end());
        mkdirs.erase(std::unique(mkdirs.begin(), mkdirs.end()), mkdirs.end());
        // 父目录先于子目录：按层数排序，同层按字节序
        std::stable_sort(mkdirs.begin(), mkdirs.end(),
                         [](const std::string& left, const std::string& right) noexcept
                         {
                             if (segment_depth(left) != segment_depth(right))
                             {
                                 return segment_depth(left) < segment_depth(right);
                             }
                             return left < right;
                         });
        plan.mkdirs = mkdirs;

        for (const std::string& directory : mkdirs)
        {
            PlanAction action;
            action.id            = std::string(kMkdirPrefix) + directory;
            action.kind          = PlanActionKind::CreateDirectory;
            action.address_scope = AddressScope::Project;
            action.path          = directory;
            action.payload_type  = PayloadType::File;
            plan.actions.push_back(std::move(action));
        }

        if (options.command == CommandKind::New)
        {
            // 登记写入只服务"新登记"：已有一条同名登记时不重写该记录，也不把它的 is_initialized
            // 降回 false（spec §8.5、§9.4）；派生缓存由 P6 的 UpdateMarker 负责
            if (preflight.entry == nullptr)
            {
                PlanAction register_action;
                register_action.id              = std::string(kRegisterPrefix) + std::string(kRegisterRelative);
                register_action.kind            = PlanActionKind::RegisterProject;
                register_action.address_scope   = AddressScope::Engine;
                register_action.path            = std::string(kRegisterRelative);
                register_action.payload_type    = PayloadType::File;
                register_action.before.is_absent = true;
                register_action.after.is_absent  = true;
                plan.actions.push_back(std::move(register_action));
            }

            if (!new_selection_bytes.empty())
            {
                PlanAction selection_action;
                selection_action.id            = make_identity_id(kWritePrefix, kSelectionFile, std::nullopt);
                selection_action.kind          = PlanActionKind::WriteSelection;
                selection_action.address_scope = AddressScope::Project;
                selection_action.path          = std::string(kSelectionFile);
                selection_action.payload_type  = PayloadType::File;
                selection_action.before.is_absent = true;
                selection_action.bytes            = new_selection_bytes;
                selection_action.after            = state_of(digest_text(new_selection_bytes));
                plan.actions.push_back(std::move(selection_action));
            }
        }

        // payload：全部内容项在第一次内容变更前备好旧字节
        for (const ContentItem& item : items)
        {
            if (!item.is_need_write || item.before.is_absent)
            {
                continue;
            }
            PlanAction action;
            action.id            = make_identity_id(kPayloadPrefix, item.path, item.fragment);
            action.kind          = item.payload_type == PayloadType::Fragment ? PlanActionKind::PayloadFragment
                                                                            : PlanActionKind::PayloadFile;
            action.address_scope = AddressScope::Project;
            action.path          = item.path;
            action.fragment      = item.fragment;
            action.payload_type  = item.payload_type;
            action.before        = item.before;
            action.after         = item.before;
            plan.actions.push_back(std::move(action));
        }
        for (const DeletionCandidate& candidate : trim.candidates)
        {
            PlanAction payload_action;
            payload_action.id            = make_identity_id(kPayloadPrefix, candidate.entry.path,
                                                            candidate.entry.fragment);
            payload_action.kind          = candidate.entry.fragment.has_value() ? PlanActionKind::PayloadFragment
                                                                               : PlanActionKind::PayloadFile;
            payload_action.address_scope = AddressScope::Project;
            payload_action.path          = candidate.entry.path;
            payload_action.fragment      = candidate.entry.fragment;
            payload_action.payload_type  = candidate.entry.fragment.has_value() ? PayloadType::Fragment
                                                                               : PayloadType::File;
            payload_action.before        = DigestState{false, candidate.disk_digest};
            payload_action.after         = payload_action.before;
            plan.actions.push_back(std::move(payload_action));

            if (!candidate.needs_backup)
            {
                continue;
            }
            PlanAction backup_action;
            backup_action.id            = make_identity_id(kBackupPrefix, candidate.entry.path,
                                                           candidate.entry.fragment);
            backup_action.kind          = candidate.entry.fragment.has_value() ? PlanActionKind::BackupFragment
                                                                              : PlanActionKind::BackupFile;
            backup_action.address_scope = AddressScope::Project;
            backup_action.path          = candidate.entry.path;
            backup_action.fragment      = candidate.entry.fragment;
            backup_action.payload_type  = payload_action.payload_type;
            backup_action.backup        = backup_relative(generation_id, candidate.entry.path,
                                                          fragment_view(candidate.entry.fragment));
            plan.actions.push_back(std::move(backup_action));
        }
        for (const ContentItem& item : items)
        {
            if (!item.is_need_write || !item.is_need_backup)
            {
                continue;
            }
            PlanAction backup_action;
            backup_action.id            = make_identity_id(kBackupPrefix, item.path, item.fragment);
            backup_action.kind          = item.payload_type == PayloadType::Fragment ? PlanActionKind::BackupFragment
                                                                                    : PlanActionKind::BackupFile;
            backup_action.address_scope = AddressScope::Project;
            backup_action.path          = item.path;
            backup_action.fragment      = item.fragment;
            backup_action.payload_type  = item.payload_type;
            backup_action.backup        = backup_relative(generation_id, item.path, fragment_view(item.fragment));
            plan.actions.push_back(std::move(backup_action));
        }
        // `clean --force` 对保留集内被本地修改的条目只留备份：这些条目本次不覆盖、不删除
        for (const ManifestEntry& entry : retained_modified)
        {
            PlanAction backup_action;
            backup_action.id            = make_identity_id(kBackupPrefix, entry.path, entry.fragment);
            backup_action.kind          = entry.fragment.has_value() ? PlanActionKind::BackupFragment
                                                                    : PlanActionKind::BackupFile;
            backup_action.address_scope = AddressScope::Project;
            backup_action.path          = entry.path;
            backup_action.fragment      = entry.fragment;
            backup_action.payload_type  = entry.fragment.has_value() ? PayloadType::Fragment : PayloadType::File;
            backup_action.backup        = backup_relative(generation_id, entry.path, fragment_view(entry.fragment));
            plan.actions.push_back(std::move(backup_action));
        }

        // 内容写入：片段按文件合并由执行阶段一次替换，计划里仍逐片段列出
        for (const ContentItem& item : items)
        {
            if (!item.is_need_write)
            {
                continue;
            }
            PlanAction action;
            action.id            = make_identity_id(kWritePrefix, item.path, item.fragment);
            action.kind          = item.fragment.has_value() ? PlanActionKind::WriteFragment : PlanActionKind::WriteFile;
            action.address_scope = AddressScope::Project;
            action.path          = item.path;
            action.fragment      = item.fragment;
            action.payload_type  = item.payload_type;
            action.before        = item.before;
            action.after         = state_of(item.planned);
            action.bytes         = item.bytes;
            action.source        = item.source;
            action.is_copy       = item.is_copy;
            plan.actions.push_back(std::move(action));
        }

        // 裁剪：逐条删除（payload 与备份已在前面排好）
        for (const DeletionCandidate& candidate : trim.candidates)
        {
            PlanAction action;
            action.id              = make_identity_id(kDeletePrefix, candidate.entry.path, candidate.entry.fragment);
            action.kind            = PlanActionKind::DeleteFile;
            action.address_scope   = AddressScope::Project;
            action.path            = candidate.entry.path;
            action.fragment        = candidate.entry.fragment;
            action.payload_type    = candidate.entry.fragment.has_value() ? PayloadType::Fragment : PayloadType::File;
            action.before          = DigestState{false, candidate.disk_digest};
            action.after.is_absent = true;
            plan.actions.push_back(std::move(action));
        }

        // 记录写入、收尾与派生缓存
        PlanAction manifest_action;
        manifest_action.id            = "record:manifest";
        manifest_action.kind          = PlanActionKind::WriteManifest;
        manifest_action.address_scope = AddressScope::Project;
        manifest_action.path          = std::string(kManifestRelative);
        manifest_action.payload_type  = PayloadType::File;
        manifest_action.bytes         = manifest_bytes;
        manifest_action.after         = state_of(digest_text(manifest_bytes));
        plan.actions.push_back(std::move(manifest_action));

        PlanAction resolved_action;
        resolved_action.id            = "record:resolved";
        resolved_action.kind          = PlanActionKind::WriteResolved;
        resolved_action.address_scope = AddressScope::Project;
        resolved_action.path          = std::string(kResolvedRelative);
        resolved_action.payload_type  = PayloadType::File;
        resolved_action.bytes         = resolved_bytes;
        resolved_action.after         = state_of(digest_text(resolved_bytes));
        plan.actions.push_back(std::move(resolved_action));

        PlanAction delete_intent;
        delete_intent.id            = std::string(kDeletePrefix) + std::string(kIntentRelative);
        delete_intent.kind          = PlanActionKind::DeleteIntent;
        delete_intent.address_scope = AddressScope::Project;
        delete_intent.path          = std::string(kIntentRelative);
        delete_intent.payload_type  = PayloadType::File;
        plan.actions.push_back(std::move(delete_intent));

        const bool has_content_change = !writes.empty() || !deletes.empty();
        if (options.command != CommandKind::Clean && (!options.marker_matches || has_content_change))
        {
            PlanAction marker_action;
            marker_action.id              = std::string(kRegisterPrefix) + std::string(kRegisterRelative);
            marker_action.kind            = PlanActionKind::UpdateMarker;
            marker_action.address_scope   = AddressScope::Engine;
            marker_action.path            = std::string(kRegisterRelative);
            marker_action.payload_type    = PayloadType::File;
            marker_action.before.is_absent = true;
            marker_action.after.is_absent  = true;
            plan.actions.push_back(std::move(marker_action));
        }

        // no-op：目标内容集合与记录摘要都已达（Engine 的标记动作不参与）
        const bool is_records_equal =
            preflight.records.has_manifest && preflight.records.has_resolved
            && digest_text(preflight.records.manifest_bytes).to_hex() == manifest_digest
            && digest_text(preflight.records.resolved_bytes).to_hex() == digest_text(resolved_bytes).to_hex();
        // `clean --force` 对被修改条目的备份是一次真实写入：此时不按 no-op 收尾，仍需 intent 与提交点
        plan.is_noop = writes.empty() && deletes.empty() && is_records_equal && retained_modified.empty();

        // no-op：两份记录的字节已与目标一致，不再写回，也不写没有可删对象的 intent
        // （spec §8.9「内容无变化的 update 不写 intent、不改两份记录」、§6.6 的 no-op clean）。
        // 派生缓存 `UpdateMarker` 不在 no-op 判定内，仍需执行：它就是"只修标记"的那条路径
        if (plan.is_noop)
        {
            const auto is_dropped = [](const PlanAction& action) noexcept
            {
                return action.kind == PlanActionKind::WriteManifest || action.kind == PlanActionKind::WriteResolved
                    || action.kind == PlanActionKind::DeleteIntent;
            };
            plan.actions.erase(std::remove_if(plan.actions.begin(), plan.actions.end(), is_dropped),
                               plan.actions.end());
        }

        if (!plan.write_conflicts.empty() || !plan.cleanup_conflicts.empty())
        {
            plan.is_valid = false;
        }
        return plan;
    }

    bool is_noop_plan(const ProjectPlan& plan) noexcept
    {
        return plan.is_noop;
    }

    bool has_plan_diff(const ProjectPlan& plan) noexcept
    {
        if (!plan.write_conflicts.empty() || !plan.cleanup_conflicts.empty())
        {
            return false;
        }
        if (plan.content_write_count != 0 || plan.delete_count != 0)
        {
            return true;
        }
        for (const PlanAction& action : plan.actions)
        {
            if (action.kind == PlanActionKind::UpdateMarker || action.kind == PlanActionKind::RegisterProject
                || action.kind == PlanActionKind::WriteSelection)
            {
                return true;
            }
        }
        return false;
    }
} // namespace ID::Project
