#include "project/Apply/Actor.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cmake/File/FragmentWriter.hpp"
#include "project/Apply/BackupStore.hpp"
#include "project/Apply/Journal.hpp"
#include "project/Apply/PayloadStore.hpp"
#include "project/Base/FileIo.hpp"
#include "project/Base/Inject.hpp"
#include "project/Base/PathText.hpp"
#include "project/Base/Text.hpp"
#include "project/Input/Registry.hpp"
#include "project/Plan/PathDigest.hpp"
#include "project/Record/RecordJson.hpp"
#include "project/Record/RecordPaths.hpp"
#include "project/Resolve/Generation.hpp"

/**
 *  §6.7 的 12 步执行。
 *
 *  顺序是唯一口径：基线复验必须在任何写入之前；payload 必须在内容变更之前；manifest 先、resolved 后；
 *  收尾先删 intent 再删 payload；标记最后写。任何"看起来更省事"的重排都会破坏恢复窗口的判定。
 *
 *  注入点：`p3.before-payload` 由 payload 写入口触发；其余六点在各自的时机显式触发。`p6.after-manifest-temp`
 *  在替换之前触发——`write_file_atomic` 把"临时文件 + 原子替换"做成一体动作，替换前失败与"临时文件已写、
 *  未替换"同属提交点之前的窗口。
 */

namespace ID::Project
{
    namespace
    {
        constexpr std::string_view kRegisterRelative = "ID/project.json";
        constexpr std::string_view kRootFileName     = "CMakeLists.txt";

        ProjectDiagnostic make_diagnostic(ProjectCategory category, std::string subject, std::string expected,
                                          std::string actual, std::string location, std::string next_action)
        {
            return ProjectDiagnostic{category, std::move(subject), std::move(expected), std::move(actual),
                                     std::move(location), std::move(next_action)};
        }

        std::optional<std::string_view> fragment_view(const std::optional<std::string>& fragment)
        {
            if (!fragment.has_value())
            {
                return std::nullopt;
            }
            return std::string_view(*fragment);
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

        // CMake 片段写入的诊断 → 项目类别（§6.7 第 3 条）
        ProjectCategory map_fragment_error(CMake::CMakeError error) noexcept
        {
            switch (error)
            {
                case CMake::CMakeError::MissingMarker:
                case CMake::CMakeError::AmbiguousMarker:
                case CMake::CMakeError::DuplicateFragment:
                    return ProjectCategory::UnmanagedContent;
                case CMake::CMakeError::TargetModified:
                    return ProjectCategory::ChangedSincePlan;
                default:
                    return ProjectCategory::WriteFailed;
            }
        }

        // 记录复验：两份记录的字节摘要与 session 基线一致（不存在的一侧按 absent 比较）
        std::optional<ProjectDiagnostic> verify_base(const Session& session)
        {
            const std::filesystem::path project_root = session.project_root();
            const Session::BaseDigests& base         = session.base_digests();

            const auto check_one = [&project_root](std::string_view relative,
                                                   const std::optional<Digest>& expected) -> std::optional<ProjectDiagnostic>
            {
                const FileRead read = read_file(record_path(project_root, relative));
                if (read.status == ReadStatus::Failed)
                {
                    return make_diagnostic(ProjectCategory::RecordInvalid, std::string(relative), "能读取该记录文件",
                                           "打开或读取失败", std::string(relative),
                                           "确认记录文件未被其它进程占用或有读权限后重跑");
                }

                const std::optional<Digest> current =
                    read.status == ReadStatus::Found ? std::optional<Digest>(digest_text(read.bytes)) : std::nullopt;
                if (current.has_value() != expected.has_value())
                {
                    return make_diagnostic(
                        ProjectCategory::ChangedSincePlan, std::string(relative),
                        expected.has_value() ? expected->to_hex() : std::string("absent"),
                        current.has_value() ? current->to_hex() : std::string("absent"), std::string(relative),
                        "计划之后记录已被改动：重跑原命令让计划按当前记录重算");
                }
                if (current.has_value() && current->bytes != expected->bytes)
                {
                    return make_diagnostic(ProjectCategory::ChangedSincePlan, std::string(relative),
                                           expected->to_hex(), current->to_hex(), std::string(relative),
                                           "计划之后记录已被改动：重跑原命令让计划按当前记录重算");
                }
                return std::nullopt;
            };

            if (const std::optional<ProjectDiagnostic> resolved = check_one(kResolvedRelative, base.resolved_digest))
            {
                return resolved;
            }
            return check_one(kManifestRelative, base.manifest_digest);
        }

        // 计划 → attempt 内容：base/target 快照、逐项 before/after 与 payload 位置
        IntentRecord make_intent(const ProjectPlan& plan, const Session& session, const RunOptions& options,
                                 const std::string& attempt)
        {
            IntentRecord record;
            record.attempt           = attempt;
            record.schema_version    = std::string(kSchemaVersion);
            record.rules_version     = std::string(kRulesVersion);
            record.command           = to_intent_command(options.command);
            record.generation_id     = plan.generation_id;
            record.selection_digest  = plan.selection_digest;
            record.plan_digest       = plan.plan_digest;
            record.base_resolved     = session.base_resolved();
            record.base_manifest     = session.base_manifest();
            record.has_base_commit   = session.base_resolved().is_present && session.base_manifest().is_present;
            record.next_argv         = options.next_argv;
            record.phase             = options.command == CommandKind::New ? "p2" : "p3";

            if (const std::string* bytes = find_record_bytes(plan, PlanActionKind::WriteResolved))
            {
                record.target_resolved.is_present = true;
                record.target_resolved.bytes      = *bytes;
                record.target_resolved.digest     = digest_text(*bytes).to_hex();
            }
            if (const std::string* bytes = find_record_bytes(plan, PlanActionKind::WriteManifest))
            {
                record.target_manifest.is_present = true;
                record.target_manifest.bytes      = *bytes;
                record.target_manifest.digest     = digest_text(*bytes).to_hex();
            }

            for (const PlanAction& action : plan.actions)
            {
                IntentOperation operation;
                operation.path          = action.path;
                operation.fragment      = action.fragment;
                operation.address_scope = action.address_scope;
                operation.payload_type  = action.payload_type;

                switch (action.kind)
                {
                    case PlanActionKind::CreateDirectory:
                        operation.kind = OperationKind::Mkdir;
                        break;
                    case PlanActionKind::RegisterProject:
                        operation.kind = OperationKind::RegisterWrite;
                        break;
                    case PlanActionKind::PayloadFile:
                    case PlanActionKind::PayloadFragment:
                        operation.kind = OperationKind::Payload;
                        break;
                    case PlanActionKind::BackupFile:
                    case PlanActionKind::BackupFragment:
                        operation.kind = OperationKind::Backup;
                        break;
                    case PlanActionKind::DeleteFile:
                        operation.kind = OperationKind::Delete;
                        break;
                    case PlanActionKind::WriteFile:
                    case PlanActionKind::WriteFragment:
                    case PlanActionKind::WriteSelection:
                        operation.kind = OperationKind::Write;
                        break;
                    default:
                        continue;   // 记录写入、intent 删除与标记重写不是 intent 的操作项
                }

                operation.id      = action.id;
                operation.before  = action.before;
                operation.after   = action.after;
                operation.backup  = action.backup;
                operation.payload = action.payload;

                // 没有摘要对象的操作项（mkdir / 登记写入 / --force 备份）在计划里是默认构造的
                // `DigestState`：`is_absent` 为假而 `hex` 为空，那是非法状态，写进记录会让下一次解析
                // 报 `record_invalid`。两侧一律按 absent 记录，表示该项没有 `(path, fragment)` 摘要
                if (operation.before.hex.empty() && !operation.before.is_absent)
                {
                    operation.before = DigestState{true, std::string()};
                }
                if (operation.after.hex.empty() && !operation.after.is_absent)
                {
                    operation.after = DigestState{true, std::string()};
                }
                if (!action.before.is_absent
                    && (operation.kind == OperationKind::Write || operation.kind == OperationKind::Delete))
                {
                    operation.payload = payload_relative(attempt, action.path, fragment_view(action.fragment));
                }
                operation.status = OperationStatus::Pending;
                record.operations.push_back(std::move(operation));
            }

            return record;
        }

        // 片段当前文本范围的原始字节（给 payload 与备份用）
        struct FragmentBytes
        {
            bool        is_ok  = false;
            std::string bytes  = {};
            std::string digest = {};
        };

        FragmentBytes read_fragment_bytes(const std::filesystem::path& project_root, std::string_view path,
                                          std::string_view fragment)
        {
            FragmentBytes result;

            CMake::FragmentWriter writer(to_utf8(project_root));
            const std::string_view names[1] = {fragment};
            const CMake::FragmentSnapshot snapshot = writer.read_fragments(std::string(path), names);
            if (snapshot.diagnostic.has_value() || snapshot.state != CMake::FragmentState::Ok)
            {
                return result;
            }
            for (const CMake::FragmentRange& range : snapshot.ranges)
            {
                if (range.name != fragment)
                {
                    continue;
                }
                result.bytes  = range.bytes;
                result.digest = CMake::fragment_range_digest(snapshot.file_bytes, range);
                result.is_ok  = !result.digest.empty();
                return result;
            }
            return result;
        }

        /**
         *  裁剪后的空目录回收：从被删文件所在目录向上逐级尝试删除"当前确实为空"的目录，到 `IDPlugin/`
         *  为止（不删 `IDPlugin/` 本身）。非空即停（用户此间的写入），删除失败不改变本次结论：
         *  目录回收只是整洁性，不是提交条件（docs/id/06 §6.7 第 5 条的"自底向上"、§6.8 第 5 条的
         *  "只删仍为空的目录、非递归、不按通配符、失败不作为失败"）。
         */
        void remove_empty_parent_directories(const std::filesystem::path& project_root, std::string_view path)
        {
            std::string_view parent = parent_relative(path);
            while (!parent.empty() && parent != kPluginRoot)
            {
                const RemoveOutcome removed = remove_empty_directory(project_root / to_native(parent));
                if (!removed.is_ok || !removed.is_removed)
                {
                    return;   // 非空或删不掉：停在这一层，更上层必然非空
                }
                parent = parent_relative(parent);
            }
        }

        // 写前复验：当前 D 必须等于计划记录的 D
        std::optional<ProjectDiagnostic> verify_current(const std::filesystem::path& project_root,
                                                       std::string_view path, const DigestState& expected)
        {
            const PathDigest current = probe_file_digest(project_root / to_native(path));
            if (!current.is_ok)
            {
                return make_diagnostic(ProjectCategory::ChangedSincePlan, std::string(path), "能读取该项的当前内容",
                                       "属性查询或读取失败", std::string(path),
                                       "确认该文件未被其它进程占用或有读权限后重跑原命令");
            }

            const std::string expected_hex = expected.is_absent ? std::string("absent")
                                                                : expected.hex;
            const std::string current_hex  = current.digest.has_value() ? current.digest->to_hex()
                                                                        : std::string("absent");
            if (expected.is_absent != !current.digest.has_value() || (!expected.is_absent && expected.hex != current_hex))
            {
                return make_diagnostic(ProjectCategory::ChangedSincePlan, std::string(path), expected_hex, current_hex,
                                       std::string(path),
                                       "计划之后磁盘内容已变：重跑原命令让计划按当前磁盘重算");
            }
            return std::nullopt;
        }

        std::string to_registry_path_text(const std::filesystem::path& project_root)
        {
            const PathCheck normalized = normalize_absolute(to_utf8(project_root), false);
            return normalized.verdict == PathVerdict::Ok ? normalized.text : to_utf8(project_root);
        }
    } // namespace

    RunResult execute_plan(Session& session, const ProjectPlan& plan, const RunOptions& options)
    {
        RunResult result;

        if (!session.is_open() || !plan.is_valid)
        {
            result.diagnostics.add(make_diagnostic(ProjectCategory::WriteFailed, options.project_name,
                                                    "会话已打开且计划有效", "会话未打开或计划无效",
                                                    std::string(kIntentRelative),
                                                    "这是实现缺陷：先 open 会话并在计划有效时执行"));
            result.exit_code = select_exit_code(result.diagnostics.span(), false);
            return result;
        }

        const std::filesystem::path project_root = session.project_root();
        const std::filesystem::path intent_path  = record_path(project_root, kIntentRelative);

        // 1. 锁内基线复验：不一致即零写退出，不写 intent
        if (const std::optional<ProjectDiagnostic> mismatch = verify_base(session))
        {
            result.diagnostics.add(*mismatch);
            result.exit_code = select_exit_code(result.diagnostics.span(), false);
            return result;
        }

        const bool needs_journal = !plan.is_noop;
        const std::string attempt = make_attempt_id(plan.generation_id);

        Journal     journal;
        PayloadStore payloads(project_root);
        BackupStore  backups(project_root, plan.generation_id);

        bool is_failed = false;
        const auto fail = [&result, &is_failed](std::optional<ProjectDiagnostic> diagnostic, ProjectCategory fallback,
                                               std::string_view subject)
        {
            is_failed = true;
            if (diagnostic.has_value())
            {
                result.diagnostics.add(*diagnostic);
                return;
            }
            result.diagnostics.add(make_diagnostic(fallback, std::string(subject), "该步骤正常完成", "失败",
                                                   std::string(subject), "按诊断给出的下一步处理后重跑"));
        };

        // 2. 写 intent（含 base/target 快照、plan_digest 与逐项 payload 位置）
        if (needs_journal)
        {
            // 注入点 P2BeforeIntent：第一笔写入（intent）之前
            if (hit_inject_point(InjectPoint::P2BeforeIntent))
            {
                fail(std::nullopt, ProjectCategory::WriteFailed, kIntentRelative);
                result.exit_code = select_exit_code(result.diagnostics.span(), false);
                return result;
            }

            const Journal::BeginResult begun = journal.begin(intent_path, make_intent(plan, session, options, attempt));
            if (!begun.is_ok)
            {
                fail(begun.diagnostic, ProjectCategory::WriteFailed, kIntentRelative);
                result.exit_code = select_exit_code(result.diagnostics.span(), false);
                return result;
            }
            result.is_intent_kept = true;
        }

        const auto advance = [&journal, &fail, needs_journal](const PlanAction& action, std::string_view actual)
        {
            if (!needs_journal)
            {
                return;   // no-op 计划不写 intent，也就没有可推进的操作项
            }
            // 记录写入、intent 删除与标记重写是控制面动作，不进 intent.operations（U5 §5.8）
            if (is_control_action(action.kind) && action.kind != PlanActionKind::RegisterProject)
            {
                return;
            }
            const Journal::BeginResult advanced = journal.advance(action.id, OperationStatus::Done, actual);
            if (!advanced.is_ok)
            {
                fail(advanced.diagnostic, ProjectCategory::WriteFailed, action.path);
            }
        };

        bool is_first_copy_pending = true;
        bool is_first_delete_pending = true;

        for (std::size_t index = 0; index < plan.actions.size(); ++index)
        {
            if (is_failed)
            {
                break;
            }

            const PlanAction& action = plan.actions[index];

            switch (action.kind)
            {
                case PlanActionKind::CreateDirectory:
                {
                    if (needs_journal)
                    {
                        const Journal::BeginResult noted = journal.note_created_directory(action.path);
                        if (!noted.is_ok)
                        {
                            fail(noted.diagnostic, ProjectCategory::WriteFailed, action.path);
                            break;
                        }
                    }
                    const RemoveOutcome created = ID::Project::create_directories(project_root / to_native(action.path));
                    if (!created.is_ok)
                    {
                        fail(created.diagnostic, ProjectCategory::WriteFailed, action.path);
                        break;
                    }
                    advance(action, std::string());
                    break;
                }

                case PlanActionKind::RegisterProject:
                {
                    const std::string root_text = to_registry_path_text(project_root);
                    RegistryEntry entry;
                    entry.name           = options.project_name;
                    entry.path           = root_text;
                    entry.is_initialized = false;

                    session.with_registry_lock(options.registry_path, [&](bool is_locked)
                                               {
                                                   if (!is_locked)
                                                   {
                                                       fail(std::nullopt, ProjectCategory::WriterConflict,
                                                            kRegisterRelative);
                                                       return;
                                                   }
                                                   const RegistryWriteOutcome written =
                                                       write_registry_entry(options.registry_path, entry, true);
                                                   if (!written.is_ok)
                                                   {
                                                       fail(written.diagnostic, ProjectCategory::WriteFailed,
                                                            kRegisterRelative);
                                                       return;
                                                   }
                    });
                    if (!is_failed)
                    {
                        advance(action, std::string());
                        // 注入点 P2AfterRegistry：登记已写、首次 plugins.json 未写
                        if (hit_inject_point(InjectPoint::P2AfterRegistry))
                        {
                            fail(std::nullopt, ProjectCategory::WriteFailed, action.path);
                        }
                    }
                    break;
                }

                case PlanActionKind::WriteSelection:
                {
                    if (const std::optional<ProjectDiagnostic> mismatch =
                            verify_current(project_root, action.path, DigestState{true, std::string()}))
                    {
                        fail(mismatch, ProjectCategory::ChangedSincePlan, action.path);
                        break;
                    }
                    WriteRequest request;
                    request.target               = project_root / to_native(action.path);
                    request.bytes                = action.bytes;
                    request.is_skip_if_identical = false;
                    request.subject              = action.path;
                    const WriteOutcome written = write_file_atomic(request);
                    if (!written.is_ok)
                    {
                        fail(written.diagnostic, ProjectCategory::WriteFailed, action.path);
                        break;
                    }
                    advance(action, digest_text(action.bytes).to_hex());
                    break;
                }

                case PlanActionKind::PayloadFile:
                {
                    PayloadStore::Result stored;
                    if (action.fragment.has_value())
                    {
                        break;
                    }
                    stored = payloads.save_file(payload_relative(attempt, action.path, std::nullopt),
                                                project_root / to_native(action.path), action.before);
                    if (!stored.is_ok)
                    {
                        fail(stored.diagnostic, ProjectCategory::WriteFailed, action.path);
                        break;
                    }
                    advance(action, stored.is_written ? stored.digest.to_hex() : std::string());
                    break;
                }

                case PlanActionKind::PayloadFragment:
                {
                    if (!action.fragment.has_value())
                    {
                        fail(std::nullopt, ProjectCategory::WriteFailed, action.path);
                        break;
                    }
                    const FragmentBytes current = read_fragment_bytes(project_root, action.path, *action.fragment);
                    if (!current.is_ok)
                    {
                        fail(make_diagnostic(ProjectCategory::ChangedSincePlan, action.path, action.before.hex,
                                             "片段标记缺失或不成对", action.path,
                                             "修正根 CMakeLists.txt 的受控标记后重跑原命令"),
                             ProjectCategory::ChangedSincePlan, action.path);
                        break;
                    }
                    const PayloadStore::Result stored =
                        payloads.save_fragment(payload_relative(attempt, action.path, action.fragment), current.bytes,
                                               action.before);
                    if (!stored.is_ok)
                    {
                        fail(stored.diagnostic, ProjectCategory::WriteFailed, action.path);
                        break;
                    }
                    advance(action, stored.is_written ? stored.digest.to_hex() : std::string());
                    break;
                }

                case PlanActionKind::BackupFile:
                {
                    const BackupStore::Result saved =
                        backups.save_file(action.path, project_root / to_native(action.path));
                    if (!saved.is_ok)
                    {
                        fail(saved.diagnostic, ProjectCategory::WriteFailed, action.path);
                        break;
                    }
                    if (const Journal::BeginResult noted = journal.note_backup(saved.backup_relative); !noted.is_ok)
                    {
                        fail(noted.diagnostic, ProjectCategory::WriteFailed, action.path);
                        break;
                    }
                    advance(action, std::string());
                    break;
                }

                case PlanActionKind::BackupFragment:
                {
                    if (!action.fragment.has_value())
                    {
                        fail(std::nullopt, ProjectCategory::WriteFailed, action.path);
                        break;
                    }
                    const FragmentBytes current = read_fragment_bytes(project_root, action.path, *action.fragment);
                    if (!current.is_ok)
                    {
                        fail(make_diagnostic(ProjectCategory::ChangedSincePlan, action.path, "片段标记成套",
                                             "片段标记缺失或不成对", action.path,
                                             "修正根 CMakeLists.txt 的受控标记后重跑原命令"),
                             ProjectCategory::ChangedSincePlan, action.path);
                        break;
                    }
                    const BackupStore::Result saved =
                        backups.save_fragment(action.path, *action.fragment, current.bytes);
                    if (!saved.is_ok)
                    {
                        fail(saved.diagnostic, ProjectCategory::WriteFailed, action.path);
                        break;
                    }
                    if (const Journal::BeginResult noted = journal.note_backup(saved.backup_relative); !noted.is_ok)
                    {
                        fail(noted.diagnostic, ProjectCategory::WriteFailed, action.path);
                        break;
                    }
                    advance(action, std::string());
                    break;
                }

                case PlanActionKind::WriteFile:
                {
                    if (const std::optional<ProjectDiagnostic> mismatch =
                            verify_current(project_root, action.path, action.before))
                    {
                        fail(mismatch, ProjectCategory::ChangedSincePlan, action.path);
                        break;
                    }

                    WriteRequest request;
                    request.target               = project_root / to_native(action.path);
                    request.is_skip_if_identical = !action.is_copy;
                    request.subject              = action.path;

                    if (action.is_copy)
                    {
                        if (is_first_copy_pending)
                        {
                            is_first_copy_pending = false;
                            if (hit_inject_point(InjectPoint::P3BeforeFirstCopy))
                            {
                                fail(std::nullopt, ProjectCategory::WriteFailed, action.path);
                                break;
                            }
                        }
                        const CopyOutcome copied =
                            copy_file_atomic(to_native(action.source), request);
                        if (!copied.is_ok)
                        {
                            fail(copied.diagnostic, ProjectCategory::WriteFailed, action.path);
                            break;
                        }
                        if (copied.digest.to_hex() != action.after.hex)
                        {
                            fail(make_diagnostic(ProjectCategory::WriteFailed, action.path, action.after.hex,
                                                 copied.digest.to_hex(), action.path,
                                                 "确认源资产未被改动后重跑原命令"),
                                 ProjectCategory::WriteFailed, action.path);
                            break;
                        }
                        if (hit_inject_point(InjectPoint::P3AfterFirstCopy))
                        {
                            fail(std::nullopt, ProjectCategory::WriteFailed, action.path);
                            break;
                        }
                        advance(action, copied.digest.to_hex());
                        break;
                    }

                    request.bytes = action.bytes;
                    // 先判路径再命中注入：注入点只在根 CMakeLists.txt 的片段/整文件写入处生效，
                    // 否则第一个非根文件写入就会把一次性注入消耗掉（一次性的语义必须与调用点一致）
                    if (action.path == kRootFileName && hit_inject_point(InjectPoint::P4BeforeFragmentWrite))
                    {
                        fail(std::nullopt, ProjectCategory::WriteFailed, action.path);
                        break;
                    }
                    const WriteOutcome written = write_file_atomic(request);
                    if (!written.is_ok)
                    {
                        fail(written.diagnostic, ProjectCategory::WriteFailed, action.path);
                        break;
                    }

                    const FileRead read_back = read_file(request.target);
                    if (read_back.status != ReadStatus::Found || digest_text(read_back.bytes).to_hex() != action.after.hex)
                    {
                        fail(make_diagnostic(ProjectCategory::WriteFailed, action.path, action.after.hex,
                                             read_back.status == ReadStatus::Found
                                                 ? digest_text(read_back.bytes).to_hex()
                                                 : std::string("回读失败"),
                                             action.path, "确认磁盘与杀毒软件未干扰写入后重跑原命令"),
                             ProjectCategory::WriteFailed, action.path);
                        break;
                    }
                    advance(action, action.after.hex);
                    break;
                }

                case PlanActionKind::WriteFragment:
                {
                    // 同一文件的两个片段相邻：一次 write_managed_blocks 替换，随后逐片段复验
                    std::vector<const PlanAction*> group;
                    std::size_t last_index = index;
                    for (std::size_t probe = index; probe < plan.actions.size(); ++probe)
                    {
                        const PlanAction& candidate = plan.actions[probe];
                        if (candidate.kind != PlanActionKind::WriteFragment || candidate.path != action.path)
                        {
                            break;
                        }
                        group.push_back(&candidate);
                        last_index = probe;
                    }
                    index = last_index;

                    if (hit_inject_point(InjectPoint::P4BeforeFragmentWrite))
                    {
                        fail(std::nullopt, ProjectCategory::WriteFailed, action.path);
                        break;
                    }

                    CMake::FragmentWriter writer(to_utf8(project_root));
                    std::vector<std::string_view> all_names;
                    all_names.reserve(group.size());
                    for (const PlanAction* item : group)
                    {
                        if (item->fragment.has_value())
                        {
                            all_names.push_back(*item->fragment);
                        }
                    }

                    const CMake::FragmentSnapshot before_snapshot =
                        writer.read_fragments(std::string(action.path), all_names);
                    if (before_snapshot.diagnostic.has_value()
                        || before_snapshot.state != CMake::FragmentState::Ok)
                    {
                        fail(make_diagnostic(ProjectCategory::ChangedSincePlan, action.path,
                                             "受控片段标记各一组且顺序正确",
                                             before_snapshot.diagnostic.has_value() ? before_snapshot.diagnostic->actual
                                                                                   : before_snapshot.detail,
                                             action.path, "修正根 CMakeLists.txt 的受控标记后重跑原命令"),
                             ProjectCategory::ChangedSincePlan, action.path);
                        break;
                    }

                    bool is_before_matched = true;
                    for (const PlanAction* item : group)
                    {
                        std::string current_hex;
                        for (const CMake::FragmentRange& range : before_snapshot.ranges)
                        {
                            if (range.name == *item->fragment)
                            {
                                current_hex = CMake::fragment_range_digest(before_snapshot.file_bytes, range);
                                break;
                            }
                        }
                        if (current_hex.empty() || current_hex != item->before.hex)
                        {
                            is_before_matched = false;
                            fail(make_diagnostic(ProjectCategory::ChangedSincePlan, action.path, item->before.hex,
                                                 current_hex.empty() ? std::string("片段范围无法定位") : current_hex,
                                                 action.path,
                                                 "计划之后根 CMakeLists.txt 的片段已变：重跑原命令"),
                                 ProjectCategory::ChangedSincePlan, action.path);
                            break;
                        }
                    }
                    if (!is_before_matched)
                    {
                        break;
                    }

                    std::vector<CMake::ManagedBlock> blocks;
                    blocks.reserve(group.size());
                    for (const PlanAction* item : group)
                    {
                        CMake::ManagedBlock block;
                        block.name    = *item->fragment;
                        block.content = item->bytes;
                        blocks.push_back(std::move(block));
                    }

                    const std::vector<CMake::ManagedWriteResult> written =
                        writer.write_managed_blocks(std::string(action.path), blocks);
                    for (const CMake::ManagedWriteResult& item : written)
                    {
                        if (item.diagnostic.has_value())
                        {
                            const ProjectCategory category = map_fragment_error(item.diagnostic->error);
                            fail(make_diagnostic(category, action.path, "受控片段替换成功",
                                                 item.diagnostic->actual, action.path,
                                                 "按诊断处理后重跑原命令；工具不整文件重建"), category,
                                 action.path);
                            break;
                        }
                    }
                    if (is_failed)
                    {
                        break;
                    }

                    // 写后逐片段复验：不把"整文件替换成功"当成"每个片段都写了"
                    const CMake::FragmentSnapshot after_snapshot =
                        writer.read_fragments(std::string(action.path), all_names);
                    if (after_snapshot.diagnostic.has_value()
                        || after_snapshot.state != CMake::FragmentState::Ok)
                    {
                        fail(make_diagnostic(ProjectCategory::WriteFailed, action.path, "写后片段标记成套",
                                             after_snapshot.diagnostic.has_value() ? after_snapshot.diagnostic->actual
                                                                                   : after_snapshot.detail,
                                             action.path, "检查根 CMakeLists.txt 后重跑原命令"),
                             ProjectCategory::WriteFailed, action.path);
                        break;
                    }
                    for (const PlanAction* item : group)
                    {
                        std::string current_hex;
                        for (const CMake::FragmentRange& range : after_snapshot.ranges)
                        {
                            if (range.name == *item->fragment)
                            {
                                current_hex = CMake::fragment_range_digest(after_snapshot.file_bytes, range);
                                break;
                            }
                        }
                        if (current_hex.empty() || current_hex != item->after.hex)
                        {
                            fail(make_diagnostic(ProjectCategory::WriteFailed, action.path, item->after.hex,
                                                 current_hex.empty() ? std::string("片段范围无法定位") : current_hex,
                                                 action.path,
                                                 "确认根 CMakeLists.txt 未被外部改写后重跑原命令"),
                                 ProjectCategory::WriteFailed, action.path);
                            break;
                        }
                        advance(*item, current_hex);
                    }
                    break;
                }

                case PlanActionKind::DeleteFile:
                {
                    if (is_first_delete_pending)
                    {
                        is_first_delete_pending = false;
                        if (hit_inject_point(InjectPoint::P5BeforeFirstDelete))
                        {
                            fail(std::nullopt, ProjectCategory::WriteFailed, action.path);
                            break;
                        }
                    }
                    if (const std::optional<ProjectDiagnostic> mismatch =
                            verify_current(project_root, action.path, action.before))
                    {
                        fail(mismatch, ProjectCategory::ChangedSincePlan, action.path);
                        break;
                    }
                    const RemoveOutcome removed = remove_file(project_root / to_native(action.path));
                    if (!removed.is_ok)
                    {
                        fail(removed.diagnostic, ProjectCategory::WriteFailed, action.path);
                        break;
                    }
                    // 逐条删除后回收当前已空的父目录：插件被整棵裁掉时不留空目录（A4/B5/K1）
                    remove_empty_parent_directories(project_root, action.path);
                    if (hit_inject_point(InjectPoint::P5AfterFirstDelete))
                    {
                        fail(std::nullopt, ProjectCategory::WriteFailed, action.path);
                        break;
                    }
                    advance(action, std::string());
                    break;
                }

                case PlanActionKind::WriteManifest:
                {
                    if (hit_inject_point(InjectPoint::P6AfterManifestTemp))
                    {
                        fail(std::nullopt, ProjectCategory::WriteFailed, action.path);
                        break;
                    }
                    WriteRequest request;
                    request.target               = project_root / to_native(action.path);
                    request.bytes                = action.bytes;
                    request.is_skip_if_identical = false;
                    request.subject              = action.path;
                    const WriteOutcome written = write_file_atomic(request);
                    if (!written.is_ok)
                    {
                        fail(written.diagnostic, ProjectCategory::WriteFailed, action.path);
                        break;
                    }
                    if (hit_inject_point(InjectPoint::P6AfterManifestRename))
                    {
                        fail(std::nullopt, ProjectCategory::WriteFailed, action.path);
                        break;
                    }
                    advance(action, action.after.hex);
                    break;
                }

                case PlanActionKind::WriteResolved:
                {
                    WriteRequest request;
                    request.target               = project_root / to_native(action.path);
                    request.bytes                = action.bytes;
                    request.is_skip_if_identical = false;
                    request.subject              = action.path;
                    const WriteOutcome written = write_file_atomic(request);
                    if (!written.is_ok)
                    {
                        fail(written.diagnostic, ProjectCategory::WriteFailed, action.path);
                        break;
                    }
                    // 提交点：resolved.json 已原子替换
                    journal.mark_committed();
                    result.is_committed = true;
                    if (hit_inject_point(InjectPoint::P6AfterResolvedRename))
                    {
                        fail(std::nullopt, ProjectCategory::WriteFailed, action.path);
                        break;
                    }
                    advance(action, action.after.hex);
                    break;
                }

                case PlanActionKind::DeleteIntent:
                {
                    const RemoveOutcome removed = remove_file(project_root / to_native(action.path));
                    if (!removed.is_ok)
                    {
                        fail(removed.diagnostic, ProjectCategory::WriteFailed, action.path);
                        break;
                    }
                    // intent 已删：此后的 advance 不得再把它写出来
                    journal.mark_removed();
                    result.is_intent_kept = false;
                    if (hit_inject_point(InjectPoint::P6AfterIntentDelete))
                    {
                        fail(std::nullopt, ProjectCategory::WriteFailed, action.path);
                        break;
                    }
                    break;
                }

                case PlanActionKind::UpdateMarker:
                {
                    const std::string root_text = to_registry_path_text(project_root);
                    RegistryEntry entry;
                    entry.name           = options.project_name;
                    entry.path           = root_text;
                    entry.is_initialized = true;

                    // 注入的失败与真实写失败走同一条诊断：intent 已删、标记仍是旧值 → write_failed + warn
                    bool is_marker_failed = hit_inject_point(InjectPoint::P6BeforeMarkerWrite);
                    if (is_marker_failed)
                    {
                        fail(std::nullopt, ProjectCategory::WriteFailed, action.path);
                    }
                    else
                    {
                        session.with_registry_lock(options.registry_path, [&](bool is_locked)
                                                   {
                                                       if (!is_locked)
                                                       {
                                                           is_marker_failed = true;
                                                           fail(std::nullopt, ProjectCategory::WriterConflict,
                                                                kRegisterRelative);
                                                           return;
                                                       }
                                                       const RegistryWriteOutcome written =
                                                           write_registry_entry(options.registry_path, entry, false);
                                                       if (!written.is_ok)
                                                       {
                                                           is_marker_failed = true;
                                                           fail(written.diagnostic, ProjectCategory::WriteFailed,
                                                                kRegisterRelative);
                                                       }
                        });
                    }
                    if (is_marker_failed)
                    {
                        // 标记是派生缓存：真源是两份记录，失败只按 write_failed 报告并附带 stale 提示
                        if (!result.is_intent_kept)
                        {
                            result.diagnostics.add(make_diagnostic(
                                ProjectCategory::RegistryMarkerStale, options.project_name,
                                std::string("is_initialized 与已提交记录一致"),
                                "标记写失败，派生缓存仍为旧值", std::string(kRegisterRelative),
                                "组装已提交；下一次运行 update 会重写该派生缓存"));
                        }
                        break;
                    }
                    advance(action, std::string());
                    break;
                }
            }
        }

        // 收尾清 payload：成功，或提交点已过（提交后 intent 与 payload 只是恢复素材，已无回滚用途）。
        // 提交点之前失败必须保留 payload——它是 `restore` 的唯一依据（spec §8.9 三段）
        if (needs_journal && (!is_failed || result.is_committed))
        {
            const RemoveOutcome discarded = payloads.discard_attempt(attempt);
            if (!discarded.is_ok && !is_failed)
            {
                fail(discarded.diagnostic, ProjectCategory::WriteFailed, kBeforeDirectory);
            }
        }

        result.exit_code = select_exit_code(result.diagnostics.span(), false);
        return result;
    }
} // namespace ID::Project
