#include "project/Apply/Recovery.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cmake/File/FragmentLayout.hpp"
#include "cmake/File/FragmentWriter.hpp"
#include "project/Apply/PayloadStore.hpp"
#include "project/Base/FileIo.hpp"
#include "project/Base/PathText.hpp"
#include "project/Base/Text.hpp"
#include "project/Input/Registry.hpp"
#include "project/Input/Selection.hpp"
#include "project/Plan/PathDigest.hpp"
#include "project/Plan/Preflight.hpp"
#include "project/Record/RecordJson.hpp"
#include "project/Record/RecordPaths.hpp"

/**
 *  `continue` 与 `restore` 的实现。
 *
 *  判定顺序不可调换：已提交窗口 → `continue` 的三条前置 → 内容窗口/冲突。`restore` 的全部判定都在
 *  预检阶段完成，预检任一失败即零写退出——这是"不半途丢恢复素材"的前提。
 */

namespace ID::Project
{
    namespace
    {
        constexpr std::string_view kSelectionFile = "plugins.json";
        constexpr std::string_view kRegisterFile  = "ID/project.json";
        constexpr std::string_view kUserSourceDir = "src";

        ProjectDiagnostic make_diagnostic(ProjectCategory category, std::string subject, std::string expected,
                                          std::string actual, std::string location, std::string next_action)
        {
            return ProjectDiagnostic{category, std::move(subject), std::move(expected), std::move(actual),
                                     std::move(location), std::move(next_action)};
        }

        bool is_content_operation(const IntentOperation& operation) noexcept
        {
            return operation.kind == OperationKind::Write || operation.kind == OperationKind::Delete;
        }

        // 用户文件与根文件本体一律不删：restore 只撤销工具 owned 的改动（spec §6.6）
        bool is_retained_user_path(std::string_view path) noexcept
        {
            return path == kSelectionFile || path == kRootCmake || is_under(path, kUserSourceDir);
        }

        /**
         *  restore 写回前的父目录重建：裁剪阶段可能已把目标所在目录删掉，写回前必须补齐父目录，
         *  否则原子写入会因目标目录不存在而失败（spec §6.6）。
         *  @param project_root 项目根绝对路径
         *  @param relative 目标的相对项目根路径，调用方已按 `normalize_relative` 校验
         *  @return 成功为空；属性查询或建目录失败时返回 `write_failed`（2）
         *  @note 只补建目标路径缺失的父目录，由浅到深逐个建；不递归删、不按通配符、不铺开其它骨架
         */
        std::optional<ProjectDiagnostic> ensure_parent_directories(const std::filesystem::path& project_root,
                                                                  std::string_view relative)
        {
            std::vector<std::string> missing;
            std::string_view         parent = parent_relative(relative);
            while (!parent.empty())
            {
                bool      is_query_ok = false;
                const bool is_existing = path_exists(project_root / to_native(parent), is_query_ok);
                if (!is_query_ok)
                {
                    return make_diagnostic(ProjectCategory::WriteFailed, std::string(parent),
                                           "目标父目录属性可查询",
                                           "属性查询失败：" + to_utf8(project_root / to_native(parent)),
                                           std::string(relative),
                                           "确认该路径未被其它进程占用或有读写权限后重跑恢复");
                }
                if (!is_existing)
                {
                    missing.emplace_back(parent);
                }
                parent = parent_relative(parent);
            }

            for (auto directory = missing.rbegin(); directory != missing.rend(); ++directory)
            {
                const RemoveOutcome created = ID::Project::create_directories(project_root / to_native(*directory));
                if (!created.is_ok)
                {
                    return created.diagnostic.has_value()
                               ? created.diagnostic
                               : make_diagnostic(ProjectCategory::WriteFailed, *directory, "目标父目录可创建",
                                                 "创建目录失败", std::string(relative),
                                                 "确认该路径未被其它进程占用或有读写权限后重跑恢复");
                }
            }
            return std::nullopt;
        }

        /**
         *  当前对象摘要：整文件条目取整文件字节，片段条目取片段文本范围。
         */
        struct ObjectProbe
        {
            bool                  is_ok  = true;
            std::optional<Digest> digest = {};
        };

        ObjectProbe probe_object(const std::filesystem::path& project_root, const IntentOperation& operation,
                                 CMake::FragmentWriter& writer)
        {
            ObjectProbe result;

            if (operation.payload_type != PayloadType::Fragment || !operation.fragment.has_value())
            {
                const PathDigest probe = probe_file_digest(project_root / to_native(operation.path));
                result.is_ok  = probe.is_ok;
                result.digest = probe.digest;
                return result;
            }

            const std::string_view names[1] = {*operation.fragment};
            const CMake::FragmentSnapshot snapshot = writer.read_fragments(operation.path, names);
            if (snapshot.diagnostic.has_value())
            {
                result.is_ok = false;
                return result;
            }
            if (snapshot.state != CMake::FragmentState::Ok)
            {
                if (snapshot.state != CMake::FragmentState::Missing)
                {
                    result.is_ok = false;
                }
                return result;
            }
            for (const CMake::FragmentRange& range : snapshot.ranges)
            {
                if (range.name != *operation.fragment)
                {
                    continue;
                }
                const std::string hex = CMake::fragment_range_digest(snapshot.file_bytes, range);
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

        std::string restore_commands(const IntentRecord& intent, const RecoveryOptions& options)
        {
            std::string normal;
            normal += "ID project";
            for (const std::string& argument : intent.next_argv)
            {
                normal += ' ';
                normal += argument;
            }
            if (normal == "ID project")
            {
                normal.clear();
            }

            std::string restore;
            restore += "ID project recover ";
            restore += options.project_name;
            restore += " --action restore";

            std::string text = "先重跑原命令";
            if (!normal.empty())
            {
                text += "（";
                text += normal;
                text += "）";
            }
            text += " 按三态续做，或运行 ";
            text += restore;
            text += " 撤销本次改动";
            return text;
        }

        // 记录回写：base 快照存在时写回两条记录，否则删除两条记录并把登记置回 false
        std::optional<ProjectDiagnostic> write_back_records(Session& session, const IntentRecord& intent,
                                                            const RecoveryOptions& options)
        {
            const std::filesystem::path project_root = session.project_root();
            const std::filesystem::path resolved_path = record_path(project_root, kResolvedRelative);
            const std::filesystem::path manifest_path = record_path(project_root, kManifestRelative);

            if (intent.has_base_commit)
            {
                WriteRequest manifest_request;
                manifest_request.target               = manifest_path;
                manifest_request.bytes                = intent.base_manifest.bytes;
                manifest_request.is_skip_if_identical = false;
                manifest_request.subject              = std::string(kManifestRelative);
                const WriteOutcome manifest_written = write_file_atomic(manifest_request);
                if (!manifest_written.is_ok)
                {
                    return manifest_written.diagnostic;
                }

                WriteRequest resolved_request;
                resolved_request.target               = resolved_path;
                resolved_request.bytes                = intent.base_resolved.bytes;
                resolved_request.is_skip_if_identical = false;
                resolved_request.subject              = std::string(kResolvedRelative);
                const WriteOutcome resolved_written = write_file_atomic(resolved_request);
                if (!resolved_written.is_ok)
                {
                    return resolved_written.diagnostic;
                }
                return std::nullopt;
            }

            const RemoveOutcome resolved_removed = remove_file(resolved_path);
            if (!resolved_removed.is_ok)
            {
                return resolved_removed.diagnostic;
            }
            const RemoveOutcome manifest_removed = remove_file(manifest_path);
            if (!manifest_removed.is_ok)
            {
                return manifest_removed.diagnostic;
            }

            const PathCheck normalized = normalize_absolute(to_utf8(project_root), false);
            RegistryEntry entry;
            entry.name           = options.project_name;
            entry.path           = normalized.verdict == PathVerdict::Ok ? normalized.text : to_utf8(project_root);
            entry.is_initialized = false;

            std::optional<ProjectDiagnostic> failure;
            session.with_registry_lock(options.registry_path, [&](bool is_locked)
                                       {
                                           if (!is_locked)
                                           {
                                               failure = make_diagnostic(
                                                   ProjectCategory::WriteFailed, std::string(kRegisterFile),
                                                   "取得登记锁", "取锁失败", std::string(kRegisterFile),
                                                   "等另一个写入者结束后重跑 recover");
                                               return;
                                           }
                                           const RegistryWriteOutcome written =
                                               write_registry_entry(options.registry_path, entry, false);
                                           if (!written.is_ok)
                                           {
                                               failure = written.diagnostic;
                                           }
            });
            return failure;
        }

        // 收尾：删 intent、删 payload、按需重写标记（提交点之后）
        std::optional<ProjectDiagnostic> finish_attempt(Session& session,
                                                        const std::filesystem::path& intent_path,
                                                        const IntentRecord& intent, const RecoveryOptions& options)
        {
            const RemoveOutcome intent_removed = remove_file(intent_path);
            if (!intent_removed.is_ok)
            {
                return intent_removed.diagnostic;
            }

            PayloadStore payloads(session.project_root());
            const RemoveOutcome discarded = payloads.discard_attempt(intent.attempt);
            if (!discarded.is_ok)
            {
                return discarded.diagnostic;
            }

            const PathCheck normalized = normalize_absolute(to_utf8(session.project_root()), false);
            RegistryEntry entry;
            entry.name           = options.project_name;
            entry.path           = normalized.verdict == PathVerdict::Ok ? normalized.text
                                                                        : to_utf8(session.project_root());
            entry.is_initialized = true;

            std::optional<ProjectDiagnostic> failure;
            session.with_registry_lock(options.registry_path, [&](bool is_locked)
                                       {
                                           if (!is_locked)
                                           {
                                               failure = make_diagnostic(
                                                   ProjectCategory::WriteFailed, std::string(kRegisterFile),
                                                   "取得登记锁", "取锁失败", std::string(kRegisterFile),
                                                   "等另一个写入者结束后重跑 recover");
                                               return;
                                           }
                                           const RegistryWriteOutcome written =
                                               write_registry_entry(options.registry_path, entry, false);
                                           if (!written.is_ok)
                                           {
                                               failure = written.diagnostic;
                                           }
            });
            return failure;
        }

        // 提交：manifest 先、resolved 后（提交点），随后收尾
        std::optional<ProjectDiagnostic> commit_target(Session& session, const std::filesystem::path& intent_path,
                                                       const IntentRecord& intent, const RecoveryOptions& options,
                                                       RecoveryResult& result)
        {
            const std::filesystem::path project_root = session.project_root();

            WriteRequest manifest_request;
            manifest_request.target               = record_path(project_root, kManifestRelative);
            manifest_request.bytes                = intent.target_manifest.bytes;
            manifest_request.is_skip_if_identical = false;
            manifest_request.subject              = std::string(kManifestRelative);
            const WriteOutcome manifest_written = write_file_atomic(manifest_request);
            if (!manifest_written.is_ok)
            {
                return manifest_written.diagnostic;
            }

            WriteRequest resolved_request;
            resolved_request.target               = record_path(project_root, kResolvedRelative);
            resolved_request.bytes                = intent.target_resolved.bytes;
            resolved_request.is_skip_if_identical = false;
            resolved_request.subject              = std::string(kResolvedRelative);
            const WriteOutcome resolved_written = write_file_atomic(resolved_request);
            if (!resolved_written.is_ok)
            {
                return resolved_written.diagnostic;
            }

            result.is_committed = true;
            return finish_attempt(session, intent_path, intent, options);
        }

        /**
         *  一次 restore 撤销项的预检结果：三态、payload 字节与将执行的动作。
         */
        struct RestoreStep
        {
            const IntentOperation* operation = nullptr;
            ThreeState             state     = ThreeState::Other;
            std::string            payload   = {};
            bool                   is_need_payload = false;
            std::string            snapshot  = {};   // 片段项：本次恢复开始时读到的整文件字节
            bool                   is_snapshot_read = false;
        };
    } // namespace

    RecoveryResult recover(Session* session, const std::filesystem::path& project_root,
                           const std::filesystem::path& intent_path, const IntentRecord& intent,
                           const RecoveryOptions& options)
    {
        RecoveryResult result;

        // `--check` 零写：不看会话、不取锁、不建目录，只按 intent 与磁盘做三态判定
        if (!options.is_check && (session == nullptr || !session->is_open()))
        {
            result.diagnostics.add(make_diagnostic(ProjectCategory::WriteFailed, options.project_name,
                                                   "会话已打开", "会话未打开", std::string(kIntentRelative),
                                                   "这是实现缺陷：写路径必须先 open 会话再收敛"));
            result.exit_code = select_exit_code(result.diagnostics.span(), false);
            return result;
        }

        // 目标快照自洽性：目标 resolved 必须与目标 manifest 字节一致，否则不得据此提交
        const RecordRead<ResolvedRecord> target_resolved = parse_resolved(intent.target_resolved.bytes);
        const RecordRead<ManifestRecord> target_manifest = parse_manifest_record(intent.target_manifest.bytes);
        if (!target_resolved.is_ok || !target_manifest.is_ok)
        {
            if (target_resolved.diagnostic.has_value())
            {
                result.diagnostics.add(*target_resolved.diagnostic);
            }
            if (target_manifest.diagnostic.has_value())
            {
                result.diagnostics.add(*target_manifest.diagnostic);
            }
            result.exit_code = select_exit_code(result.diagnostics.span(), false);
            return result;
        }
        if (const std::optional<ProjectDiagnostic> mismatch =
                check_records(target_resolved.value, target_manifest.value, intent.target_manifest.bytes))
        {
            result.diagnostics.add(*mismatch);
            result.exit_code = select_exit_code(result.diagnostics.span(), false);
            return result;
        }

        // 1. 已提交窗口：resolved 当前摘要等于目标摘要
        const FileRead resolved_read = read_file(record_path(project_root, kResolvedRelative));
        if (resolved_read.status == ReadStatus::Failed)
        {
            result.diagnostics.add(make_diagnostic(ProjectCategory::RecordInvalid, std::string(kResolvedRelative),
                                                   "能读取记录文件", "读取失败", std::string(kResolvedRelative),
                                                   "确认记录文件未被占用或有读权限后重跑"));
            result.exit_code = select_exit_code(result.diagnostics.span(), false);
            return result;
        }
        const bool is_committed_window = resolved_read.status == ReadStatus::Found
                                         && digest_text(resolved_read.bytes).to_hex() == intent.target_resolved.digest;
        result.is_committed = is_committed_window;

        if (is_committed_window)
        {
            if (options.action == RecoveryAction::Restore)
            {
                result.diagnostics.add(make_diagnostic(
                    ProjectCategory::RecoveryConflict, std::string(kIntentRelative),
                    "restore 只用于尚未提交的 attempt",
                    "组装已提交（resolved 已等于本次 attempt 的目标摘要），restore 不可用",
                    std::string(kIntentRelative),
                    "收尾只能由 recover --action continue 完成；要重新组装则运行 update"));
                result.exit_code = select_exit_code(result.diagnostics.span(), false);
                return result;
            }

            for (const IntentOperation& operation : intent.operations)
            {
                if (!is_content_operation(operation))
                {
                    continue;
                }
                RecoveryItem item;
                item.operation_id = operation.id;
                item.path         = operation.path;
                item.fragment     = operation.fragment;
                item.state        = ThreeState::After;
                item.planned      = "skip";
                result.items.push_back(std::move(item));
            }
            if (options.is_check)
            {
                result.exit_code = select_exit_code(result.diagnostics.span(), false);
                return result;
            }

            if (const std::optional<ProjectDiagnostic> failure =
                    finish_attempt(*session, intent_path, intent, options))
            {
                result.diagnostics.add(*failure);
            }
            result.exit_code = select_exit_code(result.diagnostics.span(), false);
            return result;
        }

        // 2. continue：内容已全部落盘、只剩记录提交与收尾
        if (options.action == RecoveryAction::Continue)
        {
            const SelectionFile selection = load_selection(project_root / to_native(kSelectionFile));
            if (!selection.is_present)
            {
                result.diagnostics.add(make_diagnostic(
                    ProjectCategory::ProjectConfigInvalid, std::string(kSelectionFile), "项目根内的 plugins.json",
                    "文件不存在（无法复算输入摘要）", std::string(kSelectionFile),
                    "恢复 plugins.json 后重跑；收敛前不要改动选择来源"));
                result.exit_code = select_exit_code(result.diagnostics.span(), false);
                return result;
            }
            if (!selection.is_valid)
            {
                for (const ProjectDiagnostic& diagnostic : selection.diagnostics.span())
                {
                    result.diagnostics.add(diagnostic);
                }
                result.exit_code = select_exit_code(result.diagnostics.span(), false);
                return result;
            }

            const OfflineSelection evaluated = evaluate_offline_selection(selection, target_resolved.value);
            if (!evaluated.is_ok)
            {
                result.diagnostics.add(make_diagnostic(
                    ProjectCategory::ProjectConfigInvalid, std::string(kSelectionFile),
                    "能按已接受的清单视图复算有效选择", "复算失败", std::string(kSelectionFile),
                    "修正 plugins.json 后重跑；要继续组装请运行原命令"));
                result.exit_code = select_exit_code(result.diagnostics.span(), false);
                return result;
            }
            if (evaluated.digest.to_hex() != intent.selection_digest)
            {
                result.diagnostics.add(make_diagnostic(
                    ProjectCategory::RecoveryConflict, std::string(kIntentRelative),
                    "attempt 的输入摘要与当前选择一致",
                    "selection_digest 已变（当前 " + evaluated.digest.to_hex() + "，attempt " + intent.selection_digest
                        + ')',
                    std::string(kIntentRelative),
                    "只允许 recover --action restore 撤销本次改动，再按当前选择运行 update"));
                result.exit_code = select_exit_code(result.diagnostics.span(), false);
                return result;
            }

            CMake::FragmentWriter writer(to_utf8(project_root));
            bool is_content_complete = true;
            bool is_readable         = true;
            std::vector<RecoveryItem> items;
            for (const IntentOperation& operation : intent.operations)
            {
                if (!is_content_operation(operation))
                {
                    continue;
                }
                const ObjectProbe current = probe_object(project_root, operation, writer);
                std::optional<Digest> after;
                const bool is_after_valid = digest_state_value(operation.after, after);
                if (!current.is_ok || !is_after_valid)
                {
                    is_readable = false;
                }

                RecoveryItem item;
                item.operation_id = operation.id;
                item.path         = operation.path;
                item.fragment     = operation.fragment;
                if (!current.is_ok || !is_after_valid)
                {
                    item.state = ThreeState::Other;
                }
                else if (current.digest.has_value() == after.has_value()
                         && (!current.digest.has_value() || current.digest->bytes == after->bytes))
                {
                    item.state = ThreeState::After;
                }
                else
                {
                    item.state = ThreeState::Before;
                }
                if (item.state != ThreeState::After)
                {
                    is_content_complete = false;
                }
                item.planned = item.state == ThreeState::After ? "continue" : "skip";
                items.push_back(std::move(item));
            }

            if (!is_content_complete || !is_readable)
            {
                result.items = std::move(items);
                result.diagnostics.add(make_diagnostic(
                    ProjectCategory::RecoveryConflict, std::string(kIntentRelative),
                    "所有内容项都已落盘（只差记录提交与收尾）",
                    is_readable ? "仍有内容项停在 before 或落到第三态" : "内容项摘要不可证",
                    std::string(kIntentRelative), restore_commands(intent, options)));
                result.exit_code = select_exit_code(result.diagnostics.span(), false);
                return result;
            }

            result.items = std::move(items);
            if (options.is_check)
            {
                result.exit_code = select_exit_code(result.diagnostics.span(), false);
                return result;
            }

            if (const std::optional<ProjectDiagnostic> failure =
                    commit_target(*session, intent_path, intent, options, result))
            {
                result.diagnostics.add(*failure);
            }
            result.exit_code = select_exit_code(result.diagnostics.span(), false);
            return result;
        }

        // 3. restore：预检零写
        CMake::FragmentWriter writer(to_utf8(project_root));
        PayloadStore payloads(project_root);

        std::vector<RestoreStep> steps;
        for (const IntentOperation& operation : intent.operations)
        {
            if (!is_content_operation(operation))
            {
                continue;
            }

            const ObjectProbe current = probe_object(project_root, operation, writer);
            if (!current.is_ok)
            {
                result.diagnostics.add(make_diagnostic(
                    ProjectCategory::RecoveryConflict, operation.path, "能读取该项的当前内容",
                    "属性查询失败或片段标记不成对", operation.path,
                    "修正该文件或标记后重跑 recover --action restore；不要手工删除 intent.json"));
                result.exit_code = select_exit_code(result.diagnostics.span(), false);
                return result;
            }

            const ThreeState state = judge_three_state(current.digest, operation.before, operation.after);

            RecoveryItem item;
            item.operation_id = operation.id;
            item.path         = operation.path;
            item.fragment     = operation.fragment;
            item.state        = state;
            item.planned      = state == ThreeState::After ? "restore" : "skip";
            result.items.push_back(std::move(item));

            if (state == ThreeState::Other)
            {
                result.diagnostics.add(make_diagnostic(
                    ProjectCategory::RecoveryConflict, operation.path,
                    "当前内容等于该项的 before 或 after", "第三种内容：既非 before 也非 after", operation.path,
                    "人工确认该文件后再收敛：先移走本地改动，或直接按 before 手工恢复"));
                result.exit_code = select_exit_code(result.diagnostics.span(), false);
                return result;
            }
            if (state == ThreeState::Before)
            {
                continue;   // 未执行或已撤销：跳过，不读 payload
            }

            RestoreStep step;
            step.operation = &operation;
            step.state     = state;
            if (!operation.before.is_absent)
            {
                step.is_need_payload = true;
                step.payload = payload_relative(intent.attempt, operation.path,
                                                operation.fragment.has_value()
                                                    ? std::optional<std::string_view>(*operation.fragment)
                                                    : std::nullopt);
                const PayloadStore::PayloadRead loaded = payloads.load(step.payload);
                if (!loaded.is_ok || !loaded.is_present || loaded.digest.to_hex() != operation.before.hex)
                {
                    result.diagnostics.add(make_diagnostic(
                        ProjectCategory::RecoveryConflict, operation.path, operation.before.hex,
                        loaded.is_present ? loaded.digest.to_hex() : std::string("payload 缺失"), step.payload,
                        "before-payload 缺失或损坏，无法撤销该项：不要手工删除 intent.json，"
                        "按 before 摘要手工恢复该文件后再收敛"));
                    result.exit_code = select_exit_code(result.diagnostics.span(), false);
                    return result;
                }
            }
            if (operation.payload_type == PayloadType::Fragment)
            {
                const FileRead file = read_file(project_root / to_native(operation.path));
                if (file.status != ReadStatus::Found)
                {
                    result.diagnostics.add(make_diagnostic(
                        ProjectCategory::RecoveryConflict, operation.path, "根文件存在",
                        "读取失败", operation.path, "确认该文件未被占用或有读权限后重跑恢复"));
                    result.exit_code = select_exit_code(result.diagnostics.span(), false);
                    return result;
                }
                step.snapshot           = file.bytes;
                step.is_snapshot_read   = true;
            }
            steps.push_back(std::move(step));
        }

        if (options.is_check)
        {
            result.exit_code = select_exit_code(result.diagnostics.span(), false);
            return result;
        }

        // 3.1 片段撤销：按文件一次组合、一次原子替换，片段外字节取当前磁盘内容
        std::vector<std::string> handled_paths;
        for (const RestoreStep& step : steps)
        {
            const IntentOperation& operation = *step.operation;
            if (operation.payload_type != PayloadType::Fragment || !operation.fragment.has_value())
            {
                continue;
            }
            if (std::find(handled_paths.begin(), handled_paths.end(), operation.path) != handled_paths.end())
            {
                continue;
            }
            handled_paths.push_back(operation.path);

            const FileRead file = read_file(project_root / to_native(operation.path));
            if (file.status != ReadStatus::Found)
            {
                result.diagnostics.add(make_diagnostic(ProjectCategory::RecoveryConflict, operation.path,
                                                       "根文件存在", "读取失败", operation.path,
                                                       "确认该文件未被占用或有读权限后重跑恢复"));
                result.exit_code = select_exit_code(result.diagnostics.span(), false);
                return result;
            }
            if (file.bytes != step.snapshot)
            {
                result.diagnostics.add(make_diagnostic(
                    ProjectCategory::ChangedSincePlan, operation.path, "整文件内容与恢复开始时一致",
                    "文件在恢复过程中被改动", operation.path, "确认没有其它进程改写该文件后重跑恢复"));
                result.exit_code = select_exit_code(result.diagnostics.span(), false);
                return result;
            }

            std::vector<std::string_view> names;
            for (const RestoreStep& candidate : steps)
            {
                if (candidate.operation->payload_type == PayloadType::Fragment
                    && candidate.operation->path == operation.path && candidate.operation->fragment.has_value())
                {
                    names.push_back(*candidate.operation->fragment);
                }
            }
            const CMake::FragmentLayout layout = CMake::parse_fragments(file.bytes, names);
            if (layout.state != CMake::FragmentState::Ok)
            {
                result.diagnostics.add(make_diagnostic(
                    ProjectCategory::RecoveryConflict, operation.path, "受控片段标记成套",
                    layout.detail.empty() ? std::string("标记缺失或不成对") : layout.detail, operation.path,
                    "人工修正根 CMakeLists.txt 的标记后再收敛"));
                result.exit_code = select_exit_code(result.diagnostics.span(), false);
                return result;
            }

            // 按 offset 降序处理：删除片段范围时不会让后面（更靠前）的范围失效
            std::vector<const RestoreStep*> pending;
            for (const RestoreStep& candidate : steps)
            {
                if (candidate.operation->payload_type == PayloadType::Fragment && candidate.operation->path == operation.path
                    && candidate.state == ThreeState::After)
                {
                    pending.push_back(&candidate);
                }
            }
            std::sort(pending.begin(), pending.end(),
                      [&layout](const RestoreStep* left, const RestoreStep* right) noexcept
                      {
                          const auto offset_of = [&layout](const RestoreStep* step) noexcept
                          {
                              for (const CMake::FragmentRange& range : layout.ranges)
                              {
                                  if (range.name == *step->operation->fragment)
                                  {
                                      return range.offset;
                                  }
                              }
                              return static_cast<std::size_t>(0);
                          };
                          return offset_of(left) > offset_of(right);
                      });

            std::string composed = file.bytes;
            for (const RestoreStep* item : pending)
            {
                const CMake::FragmentRange* matched = nullptr;
                for (const CMake::FragmentRange& range : layout.ranges)
                {
                    if (range.name == *item->operation->fragment)
                    {
                        matched = &range;
                        break;
                    }
                }
                if (matched == nullptr)
                {
                    continue;
                }

                if (item->operation->before.is_absent)
                {
                    // 该片段范围此前不存在：只移除本 attempt 新建的片段范围，保留文件本体与片段外字节
                    composed.erase(matched->offset, matched->length);
                    continue;
                }

                const PayloadStore::PayloadRead loaded = payloads.load(item->payload);
                if (!loaded.is_ok || !loaded.is_present)
                {
                    result.diagnostics.add(make_diagnostic(ProjectCategory::RecoveryConflict, operation.path,
                                                           "payload 可读", "payload 缺失", item->payload,
                                                           "不要手工删除 payload；按 before 摘要手工恢复后再收敛"));
                    result.exit_code = select_exit_code(result.diagnostics.span(), false);
                    return result;
                }
                composed.replace(matched->offset, matched->length, loaded.bytes);
            }

            if (const std::optional<ProjectDiagnostic> failure =
                    ensure_parent_directories(project_root, operation.path))
            {
                result.diagnostics.add(*failure);
                result.exit_code = select_exit_code(result.diagnostics.span(), false);
                return result;
            }

            WriteRequest request;
            request.target               = project_root / to_native(operation.path);
            request.bytes                = composed;
            request.is_skip_if_identical = false;
            request.subject              = operation.path;
            const WriteOutcome written = write_file_atomic(request);
            if (!written.is_ok)
            {
                result.diagnostics.add(*written.diagnostic);
                result.exit_code = select_exit_code(result.diagnostics.span(), false);
                return result;
            }

            // 逐片段复验：before 非 absent 的片段摘要必须回到 before，before 为 absent 的片段应已消失
            const CMake::FragmentSnapshot after_snapshot =
                CMake::FragmentWriter(to_utf8(project_root)).read_fragments(operation.path, names);
            for (const RestoreStep* item : pending)
            {
                std::string current_hex;
                for (const CMake::FragmentRange& range : after_snapshot.ranges)
                {
                    if (range.name == *item->operation->fragment)
                    {
                        current_hex = CMake::fragment_range_digest(after_snapshot.file_bytes, range);
                        break;
                    }
                }
                const bool is_matched = item->operation->before.is_absent ? current_hex.empty()
                                                                         : current_hex == item->operation->before.hex;
                if (!is_matched)
                {
                    result.diagnostics.add(make_diagnostic(
                        ProjectCategory::WriteFailed, operation.path,
                        item->operation->before.is_absent ? std::string("片段范围已移除")
                                                          : item->operation->before.hex,
                        current_hex.empty() ? std::string("片段不存在") : current_hex, operation.path,
                        "确认根 CMakeLists.txt 未被外部改写后重跑恢复"));
                    result.exit_code = select_exit_code(result.diagnostics.span(), false);
                    return result;
                }
            }
        }

        // 3.2 整文件撤销：before 存在时写回旧字节，before 为 absent 时删除该 owned 路径
        for (const RestoreStep& step : steps)
        {
            const IntentOperation& operation = *step.operation;
            if (operation.payload_type == PayloadType::Fragment)
            {
                continue;
            }
            if (step.state != ThreeState::After)
            {
                continue;
            }

            if (operation.before.is_absent)
            {
                if (is_retained_user_path(operation.path))
                {
                    continue;   // 用户文件与根文件本体一律保留（spec §6.6）
                }
                const RemoveOutcome removed = remove_file(project_root / to_native(operation.path));
                if (!removed.is_ok)
                {
                    result.diagnostics.add(*removed.diagnostic);
                    result.exit_code = select_exit_code(result.diagnostics.span(), false);
                    return result;
                }
                continue;
            }

            const PayloadStore::PayloadRead loaded = payloads.load(step.payload);
            if (!loaded.is_ok || !loaded.is_present)
            {
                result.diagnostics.add(make_diagnostic(ProjectCategory::RecoveryConflict, operation.path,
                                                       "payload 可读", "payload 缺失", step.payload,
                                                       "不要手工删除 payload；按 before 摘要手工恢复后再收敛"));
                result.exit_code = select_exit_code(result.diagnostics.span(), false);
                return result;
            }

            // 裁剪阶段可能已把该文件所在目录删掉：写回前按需补建父目录（spec §6.6）
            if (const std::optional<ProjectDiagnostic> failure =
                    ensure_parent_directories(project_root, operation.path))
            {
                result.diagnostics.add(*failure);
                result.exit_code = select_exit_code(result.diagnostics.span(), false);
                return result;
            }

            WriteRequest request;
            request.target               = project_root / to_native(operation.path);
            request.bytes                = loaded.bytes;
            request.is_skip_if_identical = false;
            request.subject              = operation.path;
            const WriteOutcome written = write_file_atomic(request);
            if (!written.is_ok)
            {
                result.diagnostics.add(*written.diagnostic);
                result.exit_code = select_exit_code(result.diagnostics.span(), false);
                return result;
            }

            const PathDigest restored = probe_file_digest(project_root / to_native(operation.path));
            if (!restored.is_ok || !restored.digest.has_value()
                || restored.digest->to_hex() != operation.before.hex)
            {
                result.diagnostics.add(make_diagnostic(ProjectCategory::WriteFailed, operation.path,
                                                       operation.before.hex,
                                                       restored.digest.has_value() ? restored.digest->to_hex()
                                                                                   : std::string("文件不存在"),
                                                       operation.path, "确认磁盘与杀毒软件未干扰写入后重跑恢复"));
                result.exit_code = select_exit_code(result.diagnostics.span(), false);
                return result;
            }
        }

        // 3.3 created_dirs 逆序回退：只删本次创建且当前仍为空的目录，非空即保留
        for (auto directory = intent.created_dirs.rbegin(); directory != intent.created_dirs.rend(); ++directory)
        {
            const std::string& relative = *directory;
            if (relative.empty() || normalize_relative(relative).verdict != PathVerdict::Ok)
            {
                continue;
            }
            (void)remove_empty_directory(project_root / to_native(relative));
        }
        // 3.4 记录写回：base 快照或"无已提交组装"
        if (const std::optional<ProjectDiagnostic> failure = write_back_records(*session, intent, options))
        {
            result.diagnostics.add(*failure);
            result.exit_code = select_exit_code(result.diagnostics.span(), false);
            return result;
        }

        // 3.5 收尾：删 intent 与残留 payload
        if (const RemoveOutcome removed = remove_file(intent_path); !removed.is_ok)
        {
            result.diagnostics.add(*removed.diagnostic);
            result.exit_code = select_exit_code(result.diagnostics.span(), false);
            return result;
        }
        if (const RemoveOutcome discarded = payloads.discard_attempt(intent.attempt); !discarded.is_ok)
        {
            result.diagnostics.add(*discarded.diagnostic);
        }

        result.exit_code = select_exit_code(result.diagnostics.span(), false);
        return result;
    }
} // namespace ID::Project
