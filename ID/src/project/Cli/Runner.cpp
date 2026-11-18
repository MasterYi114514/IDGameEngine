#include "project/Cli/Runner.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "project/Apply/Actor.hpp"
#include "project/Apply/Recovery.hpp"
#include "project/Apply/Session.hpp"
#include "project/Base/Error.hpp"
#include "project/Base/FileIo.hpp"
#include "project/Base/Inject.hpp"
#include "project/Base/PathText.hpp"
#include "project/Base/Text.hpp"
#include "project/Cli/CommandLine.hpp"
#include "project/Cli/Interactive.hpp"
#include "project/Emit/GeneratedTexts.hpp"
#include "project/Input/PluginCatalog.hpp"
#include "project/Input/Registry.hpp"
#include "project/Input/Selection.hpp"
#include "project/Log/Log.hpp"
#include "project/Plan/Planner.hpp"
#include "project/Plan/Preflight.hpp"
#include "project/Plan/Snapshot.hpp"
#include "project/ProjectLifeCycle.hpp"
#include "project/Record/RecordJson.hpp"
#include "project/Record/RecordPaths.hpp"
#include "project/Resolve/AssetPlan.hpp"
#include "project/Resolve/Closure.hpp"
#include "project/Resolve/EffectiveSelection.hpp"
#include "project/Resolve/Naming.hpp"
#include "project/Resolve/Resolution.hpp"

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>

    // windows.h 把 DeleteFile 定义成宏，与 PlanActionKind::DeleteFile 同名冲突；
    // 本文件只用 GetModuleFileNameW，因此就地取消该宏（不影响其它翻译单元）
    #undef DeleteFile
#else
    #include <unistd.h>
#endif

/**
 *  `ID project` 的命令编排实现（docs/id/10 §10.7、§10.8）。
 *
 *  每个写入型命令走同一条前缀：解析 → P0 预检 → P1 计划 →（`--check` 结束）→ 会话 → 执行 → 可选构建。
 *  `--check` 与真实执行共用同一份计划与同一套冲突判定，差别只在"是否落盘"；退出码统一由
 *  `select_exit_code` 给出，差异只经 `has_plan_diff` 传入。
 *
 *  `--check` 的**每个**退出分支（含 P0 失败、profile 失败、组装失败、计划无效、收敛失败）都经
 *  `finish_check_with`：先按 P0 取到的受检目标复验只读快照，再输出诊断与退出码；复验失败时不输出基于
 * 过期快照的清单，`snapshot_unstable`（2）落在同一套诊断里。
 */

namespace ID::Project
{
    namespace
    {
        // 引擎根解析的中间结果：保留已尝试的来源，失败诊断才能写清"实际取值与已尝试的来源"
        struct EngineRootResolution
        {
            std::filesystem::path    root  = {};
            std::vector<std::string> tried = {};
        };

        ProjectDiagnostic make_diagnostic(ProjectCategory category, std::string subject, std::string expected,
                                          std::string actual, std::string location, std::string next_action)
        {
            return ProjectDiagnostic{category, std::move(subject), std::move(expected), std::move(actual),
                                     std::move(location), std::move(next_action)};
        }

        // 退出码只有一个入口：诊断集中可能有 warn，warn 不参与选择
        int finish_with(std::ostream& err, const DiagnosticSet& diagnostics, bool has_plan_diff)
        {
            print_diagnostics(err, diagnostics);
            return select_exit_code(diagnostics.span(), has_plan_diff);
        }

        // `--check` 的统一退出点：结论出自读取时的状态，任何分支在输出前都复验一次只读快照
        // （spec §16 第 17 条、§10.12）。复验失败时不输出基于过期快照的清单，追加 `snapshot_unstable`（2）
        int finish_check_with(std::ostream& err, const DiagnosticSet& diagnostics,
                              std::span<const SnapshotTarget> targets, std::span<const std::string> before,
                              std::string_view command_text)
        {
            DiagnosticSet verified = diagnostics;      // 复验会追加诊断，不改调用方的结论集
            compare_snapshot(verified, targets, before, command_text);
            return finish_with(err, verified, false);
        }

        std::filesystem::path executable_directory()
        {
        #if defined(_WIN32)
            std::wstring buffer(MAX_PATH, L'\0');
            for (;;)
            {
                const DWORD length = ::GetModuleFileNameW(nullptr, buffer.data(),
                                                          static_cast<DWORD>(buffer.size()));
                if (length == 0)
                {
                    return std::filesystem::path();
                }
                if (length + 1 < buffer.size())
                {
                    buffer.resize(length);
                    break;
                }
                buffer.resize(buffer.size() * 2);
            }
            return std::filesystem::path(buffer).parent_path();
        #else
            std::vector<char> buffer(1024, '\0');
            for (;;)
            {
                const ssize_t length = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
                if (length <= 0)
                {
                    return std::filesystem::path();
                }
                if (static_cast<std::size_t>(length) + 1 < buffer.size())
                {
                    buffer.resize(static_cast<std::size_t>(length));
                    break;
                }
                buffer.resize(buffer.size() * 2);
            }
            return std::filesystem::path(std::string(buffer.data(), buffer.size())).parent_path();
        #endif
        }

        // 引擎根的判据：必须有 ID/；IDPlugin/ 缺省时只认已经有本机登记的引擎根。
        // `build`/`recover`/`clean`/`list` 不读源插件库，源插件目录整体不可用时它们仍要能定位项目
        // （spec §8.3、§12.1）；`new`/`update` 若真的缺 IDPlugin/ 由 P0 的"可读的插件目录"给码。不猜、不向上搜索
        bool has_project_directories(const std::filesystem::path& root)
        {
            if (root.empty())
            {
                return false;
            }

            bool       is_ok = false;
            const bool has_id = is_directory(root / "ID", is_ok);
            if (!is_ok || !has_id)
            {
                return false;
            }

            const bool has_plugins = is_directory(root / "IDPlugin", is_ok);
            if (is_ok && has_plugins)
            {
                return true;
            }
            const bool has_registry = is_regular_file(root / "ID" / "project.json", is_ok);
            return is_ok && has_registry;
        }

        EngineRootResolution resolve_engine_root_detail(std::string_view override_root)
        {
            EngineRootResolution result;

            const auto consider = [&result](const std::filesystem::path& candidate, std::string_view source)
            {
                if (candidate.empty())
                {
                    return false;
                }
                result.tried.push_back(std::string(source) + " = " + to_utf8(candidate));
                if (!has_project_directories(candidate))
                {
                    return false;
                }
                result.root = candidate;
                return true;
            };

            if (!override_root.empty())
            {
                const PathCheck normalized = normalize_absolute(override_root, false);
                if (normalized.verdict != PathVerdict::Ok)
                {
                    result.tried.push_back("override_root（无法规范化）= " + std::string(override_root));
                    return result;
                }
                if (consider(to_native(normalized.text), "override_root"))
                {
                    return result;
                }
            }

            const char* const environment = std::getenv("ID_ENGINE_ROOT");
            if (environment != nullptr && environment[0] != '\0')
            {
                const PathCheck normalized = normalize_absolute(environment, false);
                if (normalized.verdict == PathVerdict::Ok && consider(to_native(normalized.text), "ID_ENGINE_ROOT"))
                {
                    return result;
                }
                result.tried.push_back(std::string("ID_ENGINE_ROOT（无法规范化）= ") + environment);
            }

            const std::filesystem::path executable_dir = executable_directory();
            if (!executable_dir.empty() && executable_dir.filename() == "bin")
            {
                if (consider(executable_dir.parent_path(), "可执行文件目录的父目录"))
                {
                    return result;
                }
            }
            else if (!executable_dir.empty())
            {
                result.tried.push_back("可执行文件目录（不是 bin）= " + to_utf8(executable_dir));
            }

            std::error_code error;
            const std::filesystem::path current = std::filesystem::current_path(error);
            if (!error)
            {
                consider(current, "当前工作目录");
            }
            return result;
        }

        CommandKind to_command_kind(Subcommand subcommand) noexcept
        {
            switch (subcommand)
            {
                case Subcommand::New:       return CommandKind::New;
                case Subcommand::Update:    return CommandKind::Update;
                case Subcommand::Build:     return CommandKind::Build;
                case Subcommand::Clean:     return CommandKind::Clean;
                case Subcommand::Recover:   return CommandKind::Recover;
                case Subcommand::List:      return CommandKind::List;
            }
            return CommandKind::Update;
        }

        // CLI 的直接选择：`linkage` 未写出时按清单 requirements 展开缺省形态（spec §10.2 的缺省优先级）
        std::vector<PluginSpec> fill_linkage_defaults(std::vector<PluginSpec> specs, const PluginCatalog& catalog)
        {
            for (PluginSpec& spec : specs)
            {
                if (spec.linkage.has_value())
                {
                    continue;
                }

                const CatalogEntry* entry = find_plugin(catalog, spec.name);
                if (entry == nullptr)
                {
                    // 查不到时留给解析阶段报 plugin_unknown：这里不猜形态，也不阻断
                    continue;
                }
                spec.linkage = default_linkage(entry->manifest.requirement);
            }
            return specs;
        }

        SelectionFile make_selection_file(std::string_view project_name, std::span<const PluginSpec> specs)
        {
            SelectionFile selection;
            selection.is_present = false;
            selection.is_valid   = true;
            selection.name       = std::string(project_name);

            for (const PluginSpec& spec : specs)
            {
                SelectionPlugin plugin;
                plugin.name    = spec.name;
                plugin.linkage = spec.linkage.value_or(default_linkage(Requirement::Any));
                plugin.options = spec.options;
                selection.plugins.push_back(std::move(plugin));
            }
            return selection;
        }

        // 组装结果：解析 → 命名 → 资产 → 生成文本摘要回填
        struct AssemblyOutcome
        {
            ResolvedAssembly  assembly    = {};
            DiagnosticSet     diagnostics = {};
            bool              is_ok       = false;
        };

        AssemblyOutcome build_assembly(const PreflightResult& preflight, const std::filesystem::path& engine_root)
        {
            AssemblyOutcome result;
            const std::string project_name = preflight.project_name;

            EffectiveSelection selection = expand_direct_selection(preflight.selection, preflight.catalog);
            if (!selection.is_valid)
            {
                result.diagnostics = selection.diagnostics;
                return result;
            }

            Resolution resolution = resolve_closure(selection, preflight.catalog);
            if (!resolution.is_valid)
            {
                result.diagnostics = resolution.diagnostics;
                return result;
            }

            NamingPlan naming = plan_naming(project_name, resolution, preflight.catalog);
            if (!naming.is_valid)
            {
                result.diagnostics = naming.diagnostics;
                return result;
            }

            AssetPlan assets = plan_assets(preflight.project_root, engine_root, resolution, preflight.catalog);
            if (!assets.is_valid)
            {
                result.diagnostics = assets.diagnostics;
                return result;
            }

            result.assembly.resolution = std::move(resolution);
            result.assembly.naming     = std::move(naming);
            result.assembly.assets     = std::move(assets);

            // 生成文本与摘要回填：generation 身份、manifest 摘要与计划写入项三处必须取同一份摘要
            const GeneratedTexts texts = emit_all(result.assembly, project_name);
            if (!texts.is_valid)
            {
                result.diagnostics = texts.diagnostics;
                return result;
            }
            apply_generated_digests(texts, result.assembly.assets);

            result.assembly.is_valid = true;
            result.is_ok             = true;
            return result;
        }

        // 计划选项的磁盘事实：根 CMakeLists.txt 是否已存在、派生缓存是否已与记录一致
        PlanOptions make_plan_options(const CommandRequest& request, CommandKind command,
                                     const PreflightResult& preflight)
        {
            PlanOptions options;
            options.command        = command;
            // new 只接受该参数、不据此覆盖任何内容：它只作用于更新与裁剪的冲突处理（spec §9.1）
            options.has_force      = request.is_force && command != CommandKind::New;
            options.is_check       = request.is_check;
            options.marker_matches = true;

            bool       is_ok = false;
            const bool has_root_cmake =
                is_regular_file(preflight.project_root / "CMakeLists.txt", is_ok);
            options.has_root_cmake = is_ok && has_root_cmake;

            if (preflight.entry != nullptr)
            {
                const bool is_committed = preflight.records.has_resolved && preflight.records.is_trusted;
                options.marker_matches  = preflight.entry->is_initialized == is_committed;
            }
            return options;
        }

        // `--check` 诊断"下一步"里可复制的重跑形式：命令种类 → 完整调用文本
        std::string_view check_command_text(CommandKind command) noexcept
        {
            switch (command)
            {
                case CommandKind::New:      return "ID project new --check";
                case CommandKind::Update:   return "ID project update --check";
                case CommandKind::Build:    return "ID project build --check";
                case CommandKind::Clean:    return "ID project clean --check";
                case CommandKind::Recover:  return "ID project recover --check";
                case CommandKind::List:     return "ID project list";
            }
            return "ID project update --check";
        }

        // 逐文件动作行：动词由计划里的 before/after 关系给出（相同即 unchanged，跳过写入）
        void print_plan_actions(std::ostream& out, const ProjectPlan& plan)
        {
            for (const PlanAction& action : plan.actions)
            {
                switch (action.kind)
                {
                    case PlanActionKind::WriteFile:
                    case PlanActionKind::WriteFragment:
                        print_action_result(out, action.before.hex == action.after.hex ? "unchanged" : "written",
                                            action.path);
                        break;
                    case PlanActionKind::DeleteFile:
                        print_action_result(out, "removed", action.path);
                        break;
                    case PlanActionKind::BackupFile:
                    case PlanActionKind::BackupFragment:
                        print_action_result(out, "backup", action.path);
                        break;
                    default:
                        break;
                }
            }
        }

        // 构建 profile 的唯一解析入口：`build` 与带 `--build` 的 new/update 共用同一份校验（spec §12.1）
        ProfileResolution resolve_build_profile(const CommandRequest& request,
                                                const std::filesystem::path& project_root, bool is_build_command)
        {
            ProfileRequest profile_request;
            profile_request.generator        = request.generator;
            profile_request.toolchain        = request.toolchain;
            profile_request.arch             = request.arch;
            profile_request.config           = request.config;
            profile_request.cmake            = request.cmake;
            profile_request.is_build_command = is_build_command;
            profile_request.project_root     = project_root;
            return resolve_profile(profile_request);
        }

        // 构建请求：记录属于当前已提交组装，profile 由调用方解析并校验
        BuildRequest make_build_request(const CommandRequest& request, const std::filesystem::path& project_root,
                                        const BuildProfile& profile, const ResolvedRecord& resolved)
        {
            BuildRequest build;
            build.source_command  = to_command_kind(request.command);
            build.project_name    = resolved.project_name.empty() ? request.name : resolved.project_name;
            build.project_root    = project_root;
            build.profile         = profile;
            build.resolved        = resolved;
            build.manifest_digest = resolved.manifest_digest;
            build.is_check        = request.is_check;
            return build;
        }

        // 写入型命令的公共前缀（new / update / clean）：预检 → 组装 → 计划 → 会话 → 执行
        int run_write_command(const CommandRequest& request, const RunnerContext& context,
                              const std::filesystem::path& engine_root, bool has_selection_override,
                              const SelectionFile& selection_override, const std::vector<std::string>& next_argv,
                              std::ostream& out, std::ostream& err)
        {
            const CommandKind command = to_command_kind(request.command);

            PreflightRequest preflight_request;
            preflight_request.command                = command;
            preflight_request.project_name           = request.name;
            preflight_request.project_path_text      = request.path;
            preflight_request.is_build_requested     = request.is_build;
            preflight_request.is_check               = request.is_check;
            preflight_request.has_force              = request.is_force;
            preflight_request.engine_root            = engine_root;
            preflight_request.registry_path          = context.registry_path;
            preflight_request.plugin_root            = engine_root / to_native(kPluginRoot);
            preflight_request.selection_override     = selection_override;
            preflight_request.has_selection_override = has_selection_override;

            const PreflightResult preflight = run_preflight(preflight_request);

            // 本命令的每个退出分支都走同一个点：`--check` 在输出任何结论之前复验只读快照
            const auto finish = [&](const DiagnosticSet& diagnostics)
            {
                if (!request.is_check)
                {
                    return finish_with(err, diagnostics, false);
                }
                return finish_check_with(err, diagnostics, preflight.snapshot_targets,
                                         preflight.snapshot_before, check_command_text(command));
            };

            if (!preflight.is_ok)
            {
                return finish(preflight.diagnostics);
            }

            // 请求构建时 profile 属 P0：在 P1 与任何写入（含会话取锁、bootstrap 与登记）之前校验，
            // 失败按 build_environment_unsupported / build_invoke_failed（4）零写退出（spec §8.3、§9.3）
            ProfileResolution profile;
            if (request.is_build)
            {
                profile = resolve_build_profile(request, preflight.project_root, false);
                if (!profile.is_ok)
                {
                    DiagnosticSet diagnostics = preflight.diagnostics;
                    for (const ProjectDiagnostic& diagnostic : profile.diagnostics.span())
                    {
                        diagnostics.add(diagnostic);
                    }
                    return finish(diagnostics);
                }
            }
            print_diagnostics(err, preflight.diagnostics);

            ResolvedAssembly assembly;
            if (command == CommandKind::Clean)
            {
                // clean 不读 plugins.json、不读源插件目录：计划只用到项目名
                assembly.resolution.selection.project_name = preflight.project_name;
                assembly.is_valid                          = true;
            }
            else
            {
                AssemblyOutcome assembled = build_assembly(preflight, engine_root);
                if (!assembled.is_ok)
                {
                    return finish(assembled.diagnostics);
                }
                assembly = std::move(assembled.assembly);
            }

            // 闭包补齐说明（spec §10.1）：补齐引入的插件不是警告，也不改写 plugins.json，
            // 只在解析成功后以 info 说明一次，写清被补齐的插件与声明该依赖的下游
            for (const ClosedBackPlugin& addition : assembly.resolution.closed_back)
            {
                IDPROJECT_LOG_INFO("闭包补齐: {} 由 {} 的 dependencies 引入（未改写 plugins.json）", addition.plugin,
                                   addition.required_by);
            }

            const PlanOptions options       = make_plan_options(request, command, preflight);
            const ProjectPlan  plan          = build_plan(assembly, preflight, options);
            const bool         is_plan_valid = plan.is_valid;

            // 有冲突的计划仍要把三类清单列出来（spec §8.4）：`--check` 的退出码由冲突类别给出，
            // 调用方要靠清单看到"哪些路径是冲突、哪些本来要写"。只有"计划无效且没有冲突"才提前结束
            const bool has_conflicts = !plan.write_conflicts.empty() || !plan.cleanup_conflicts.empty();

            if (!is_plan_valid && !has_conflicts)
            {
                return finish(plan.diagnostics);
            }

            if (request.is_check)
            {
                // 只读快照复验：清单出自读取时的状态，输出前确认状态未变（spec §16 第 17 条、§10.12）。
                // 摘要变化时不输出基于过期快照的清单，只报 snapshot_unstable（2）
                DiagnosticSet diagnostics     = plan.diagnostics;
                const bool    is_snapshot_ok  =
                    compare_snapshot(diagnostics, preflight.snapshot_targets, preflight.snapshot_before,
                                     check_command_text(command));
                if (is_snapshot_ok)
                {
                    print_check_report(out, plan);
                }
                print_diagnostics(err, diagnostics);
                return select_exit_code(diagnostics.span(), is_snapshot_ok && has_plan_diff(plan));
            }

            if (!is_plan_valid)
            {
                return finish_with(err, plan.diagnostics, false);
            }

            Session              session;
            const Session::OpenResult opened =
                session.open(preflight.project_root, command == CommandKind::New);
            if (!opened.is_ok)
            {
                DiagnosticSet diagnostics = plan.diagnostics;
                if (opened.diagnostic.has_value())
                {
                    diagnostics.add(*opened.diagnostic);
                }
                return finish_with(err, diagnostics, false);
            }

            RunOptions run_options;
            run_options.command        = command;
            run_options.has_force      = request.is_force && command != CommandKind::New;
            run_options.is_build_after = request.is_build;
            // 收敛命令按本次实际参数记录：诊断里的"重跑原命令"必须是可直接复制的形式（spec §6.4）
            run_options.next_argv      = next_argv;
            run_options.registry_path  = context.registry_path;
            run_options.project_name   = preflight.project_name;

            const RunResult executed = execute_plan(session, plan, run_options);
            if (executed.exit_code == kExitSuccess)
            {
                print_plan_actions(out, plan);
            }

            DiagnosticSet diagnostics = plan.diagnostics;
            for (const ProjectDiagnostic& diagnostic : executed.diagnostics.span())
            {
                diagnostics.add(diagnostic);
            }

            if (executed.exit_code != kExitSuccess)
            {
                return finish_with(err, diagnostics, false);
            }

            // 带 --build 时只输出一份收尾摘要（构建阶段再打印产物与记录）
            if (!request.is_build)
            {
                print_success_summary(out, executed, nullptr);
                print_limitations(out, plan.next_resolved.limitations);
            }
            print_diagnostics(err, diagnostics);

            if (!request.is_build)
            {
                return select_exit_code(diagnostics.span(), false);
            }

            // `--build`：仍在同一个项目锁内进入 P7（profile 已在 P0 校验通过）
            const BuildRequest build_request =
                make_build_request(request, preflight.project_root, profile.profile, plan.next_resolved);
            const BuildOutcome outcome = run_build(build_request);

            print_success_summary(out, executed, &outcome);
            return finish_with(err, outcome.diagnostics, false);
        }

        int run_build_command(const CommandRequest& request, const RunnerContext& context,
                              const std::filesystem::path& engine_root, std::ostream& out, std::ostream& err)
        {
            PreflightRequest preflight_request;
            preflight_request.command            = CommandKind::Build;
            preflight_request.project_name       = request.name;
            preflight_request.is_build_requested = true;
            preflight_request.is_check           = request.is_check;
            preflight_request.engine_root        = engine_root;
            preflight_request.registry_path      = context.registry_path;
            preflight_request.plugin_root        = engine_root / to_native(kPluginRoot);

            const PreflightResult preflight = run_preflight(preflight_request);

            // 本命令的每个退出分支都走同一个点：`--check` 在输出任何结论之前复验只读快照
            const auto finish = [&](const DiagnosticSet& diagnostics)
            {
                if (!request.is_check)
                {
                    return finish_with(err, diagnostics, false);
                }
                return finish_check_with(err, diagnostics, preflight.snapshot_targets,
                                         preflight.snapshot_before, check_command_text(CommandKind::Build));
            };

            if (!preflight.is_ok)
            {
                return finish(preflight.diagnostics);
            }
            print_diagnostics(err, preflight.diagnostics);

            const ProfileResolution profile = resolve_build_profile(request, preflight.project_root, true);
            if (!profile.is_ok)
            {
                DiagnosticSet diagnostics = preflight.diagnostics;
                for (const ProjectDiagnostic& diagnostic : profile.diagnostics.span())
                {
                    diagnostics.add(diagnostic);
                }
                return finish(diagnostics);
            }

            const BuildRequest build_request =
                make_build_request(request, preflight.project_root, profile.profile, preflight.records.resolved);

            if (request.is_check)
            {
                const BuildOutcome checked = run_build(build_request);

                // 只读快照复验：构建请求出自读取时的记录与选择，输出前确认状态未变（spec §16 第 17 条）
                DiagnosticSet diagnostics    = checked.diagnostics;
                const bool    is_snapshot_ok =
                    compare_snapshot(diagnostics, preflight.snapshot_targets, preflight.snapshot_before,
                                     check_command_text(CommandKind::Build));
                if (is_snapshot_ok)
                {
                    out << "将执行一次构建（--check，不执行）:\n";
                    out << "    configure: " << render_argv_for_display(checked.record.configure_argv) << '\n';
                    out << "    build: " << render_argv_for_display(checked.record.build_argv) << '\n';
                    out << "    构建目录: " << to_utf8(profile.profile.build_dir) << '\n';
                    print_limitations(out, preflight.records.resolved.limitations);
                }
                return finish_with(err, diagnostics, false);
            }

            Session session;
            const Session::OpenResult opened = session.open(preflight.project_root, false);
            if (!opened.is_ok)
            {
                DiagnosticSet diagnostics = preflight.diagnostics;
                if (opened.diagnostic.has_value())
                {
                    diagnostics.add(*opened.diagnostic);
                }
                return finish_with(err, diagnostics, false);
            }

            const BuildOutcome outcome = run_build(build_request);
            if (outcome.exit_code == kExitSuccess)
            {
                // build 不改变组装：摘要里"已提交"指当前已提交组装仍然有效
                RunResult summary;
                summary.exit_code    = outcome.exit_code;
                summary.is_committed = true;
                print_success_summary(out, summary, &outcome);
                print_limitations(out, preflight.records.resolved.limitations);
            }
            return finish_with(err, outcome.diagnostics, false);
        }

        int run_recover_command(const CommandRequest& request, const RunnerContext& context,
                                const std::filesystem::path& engine_root, std::ostream& out, std::ostream& err)
        {
            PreflightRequest preflight_request;
            preflight_request.command            = CommandKind::Recover;
            preflight_request.project_name       = request.name;
            preflight_request.is_check           = request.is_check;
            preflight_request.has_restore_action = request.action == "restore";
            preflight_request.engine_root        = engine_root;
            preflight_request.registry_path      = context.registry_path;
            preflight_request.plugin_root        = engine_root / to_native(kPluginRoot);

            const PreflightResult preflight = run_preflight(preflight_request);

            // 本命令的每个失败分支都走同一个点：`--check` 在输出任何结论之前复验只读快照
            const auto finish = [&](const DiagnosticSet& diagnostics)
            {
                if (!request.is_check)
                {
                    return finish_with(err, diagnostics, false);
                }
                return finish_check_with(err, diagnostics, preflight.snapshot_targets,
                                         preflight.snapshot_before, check_command_text(CommandKind::Recover));
            };

            if (!preflight.is_ok)
            {
                return finish(preflight.diagnostics);
            }
            print_diagnostics(err, preflight.diagnostics);

            const std::string next_action =
                request.action == "restore"
                    ? "没有可收敛的 attempt：直接重跑原命令（如 ID project update " + request.name + "）"
                    : "没有可收敛的 attempt：直接重跑原命令（如 ID project update " + request.name
                          + "）；若只是想撤销上次改动，用 recover " + request.name + " --action restore";

            // 没有 attempt 就没有可收敛的对象：按 recovery_conflict（2）报，不当作"无操作成功"
            if (!preflight.records.has_intent)
            {
                DiagnosticSet diagnostics = preflight.diagnostics;
                diagnostics.add(make_diagnostic(ProjectCategory::RecoveryConflict, request.name,
                                                "存在未完成的 intent.json 可供收敛",
                                                "项目内没有未完成的 attempt", std::string(kIntentRelative),
                                                next_action));
                return finish(diagnostics);
            }

            RecoveryOptions recovery_options;
            recovery_options.action =
                request.action == "restore" ? RecoveryAction::Restore : RecoveryAction::Continue;
            recovery_options.is_check      = request.is_check;
            recovery_options.registry_path = context.registry_path;
            recovery_options.project_name  = preflight.project_name;

            const std::filesystem::path intent_path =
                record_path(preflight.project_root, kIntentRelative);

            // `--check` 零写：不打开会话（不取锁、不建 IDGenerated/、不写临时文件），只出逐项结论
            Session session;
            if (!request.is_check)
            {
                const Session::OpenResult opened = session.open(preflight.project_root, false);
                if (!opened.is_ok)
                {
                    DiagnosticSet diagnostics = preflight.diagnostics;
                    if (opened.diagnostic.has_value())
                    {
                        diagnostics.add(*opened.diagnostic);
                    }
                    return finish(diagnostics);
                }
            }

            const RecoveryResult recovered =
                recover(request.is_check ? nullptr : &session, preflight.project_root, intent_path,
                        preflight.records.intent, recovery_options);

            DiagnosticSet diagnostics = preflight.diagnostics;
            for (const ProjectDiagnostic& diagnostic : recovered.diagnostics.span())
            {
                diagnostics.add(diagnostic);
            }

            if (recovered.exit_code != kExitSuccess)
            {
                return finish(diagnostics);
            }

            // `--check` 的只读快照复验：逐项结论出自读取时的记录，输出前确认状态未变（spec §16 第 17 条）；
            // 摘要变化时不输出基于过期快照的结论，只保留诊断与 snapshot_unstable（2）
            if (request.is_check
                && !compare_snapshot(diagnostics, preflight.snapshot_targets, preflight.snapshot_before,
                                     check_command_text(CommandKind::Recover)))
            {
                return finish_with(err, diagnostics, false);
            }

            for (const RecoveryItem& item : recovered.items)
            {
                print_action_result(out, item.planned, item.path);
            }
            out << "收敛动作: " << request.action << '\n';
            out << "组装: " << (recovered.is_committed ? "已提交" : "未提交") << '\n';
            print_limitations(out, preflight.records.resolved.limitations);
            return finish_with(err, diagnostics, false);
        }

        int run_list_command(const RunnerContext& context, std::ostream& out, std::ostream& err)
        {
            DiagnosticSet     diagnostics;
            const std::string snapshot_subject = to_utf8(context.registry_path);

            const FileRead before = read_file(context.registry_path);
            const RegistryFile registry = load_registry(context.registry_path);

            if (before.status == ReadStatus::Failed)
            {
                diagnostics.add(make_diagnostic(ProjectCategory::RegistryInvalid, snapshot_subject,
                                                "能读取本机项目登记", "读取失败", snapshot_subject,
                                                "确认登记文件未被其它进程占用或有读权限后重跑"));
                return finish_with(err, diagnostics, false);
            }

            for (const ProjectDiagnostic& diagnostic : registry.diagnostics.span())
            {
                diagnostics.add(diagnostic);
            }
            if (!registry.is_valid)
            {
                return finish_with(err, diagnostics, false);
            }

            std::vector<RegistryEntry> entries = registry.entries;
            std::sort(entries.begin(), entries.end(),
                      [](const RegistryEntry& left, const RegistryEntry& right) noexcept
                      {
                          return left.name < right.name;
                      });

            for (const RegistryEntry& entry : entries)
            {
                const std::filesystem::path project_root = to_native(entry.path);

                bool       is_ok    = false;
                const bool has_path = is_directory(project_root, is_ok);

                bool is_duplicate = false;
                for (const RegistryEntry& other : entries)
                {
                    if (&other == &entry)
                    {
                        continue;
                    }
                    if (other.name == entry.name || other.path == entry.path)
                    {
                        is_duplicate = true;
                        break;
                    }
                }

                std::string record_state;
                if (has_path)
                {
                    const FileRead resolved_read = read_file(record_path(project_root, kResolvedRelative));
                    const FileRead manifest_read = read_file(record_path(project_root, kManifestRelative));
                    const bool     has_resolved  = resolved_read.status == ReadStatus::Found;
                    const bool     has_manifest  = manifest_read.status == ReadStatus::Found;

                    // 两份记录都不存在时没有可判定的记录：不给状态列，改由 initialized|pending 表达
                    if (has_resolved || has_manifest)
                    {
                        bool is_ok_records = has_resolved && has_manifest;
                        if (is_ok_records)
                        {
                            const RecordRead<ResolvedRecord> resolved = parse_resolved(resolved_read.bytes);
                            const RecordRead<ManifestRecord> manifest = parse_manifest_record(manifest_read.bytes);
                            is_ok_records =
                                resolved.is_ok && manifest.is_ok
                                && !check_records(resolved.value, manifest.value, manifest_read.bytes).has_value();
                        }
                        record_state = is_ok_records ? "record: ok" : "record: invalid";
                    }
                }

                out << entry.name << "  " << entry.path << "  "
                    << (entry.is_initialized ? "initialized" : "pending");
                if (!has_path)
                {
                    out << "  missing";
                }
                if (is_duplicate)
                {
                    out << "  duplicate";
                }
                if (!record_state.empty())
                {
                    out << "  " << record_state;
                }
                out << '\n';
            }

            // 只读快照：读取期间登记文件被改写即快照不成立（与 --check 共用同一份比较实现）
            const std::vector<SnapshotTarget> snapshot_targets{
                SnapshotTarget{context.registry_path, snapshot_subject, "登记文件"}};
            const std::vector<std::string> snapshot_before{snapshot_digest(before)};
            compare_snapshot(diagnostics, snapshot_targets, snapshot_before,
                             check_command_text(CommandKind::List));
            return finish_with(err, diagnostics, false);
        }

        int run_new_command(const CommandRequest& request, const RunnerContext& context,
                            const std::filesystem::path& engine_root, const std::vector<std::string>& next_argv,
                            std::ostream& out, std::ostream& err)
        {
            bool                      has_selection = request.is_empty || !request.plugins.empty();
            std::vector<PluginSpec>   specs         = request.plugins;

            // 目标项目根（词法规范化）：只用于"plugins.json 是否已存在"的前置判定
            std::filesystem::path project_root;
            const PathCheck       normalized_path = normalize_absolute(request.path, false);
            if (normalized_path.verdict == PathVerdict::Ok)
            {
                project_root = to_native(normalized_path.text);
            }

            // `--check` 的退出也经过"读取后复验"：以下三个结论在 P0 之前产生，依据只有项目内的状态文件，
            // 因此在读它们之前按同一张表取一次快照；登记与记录此时还没被读，P0 起来后会补上那一段
            std::vector<SnapshotTarget> snapshot_targets;
            std::vector<std::string>    snapshot_before;
            if (request.is_check && !project_root.empty())
            {
                snapshot_targets = check_snapshot_targets(project_root, CommandKind::New);
                snapshot_before  = sample_snapshot(snapshot_targets);
            }

            const auto finish_early = [&](const DiagnosticSet& diagnostics)
            {
                if (!request.is_check)
                {
                    return finish_with(err, diagnostics, false);
                }
                return finish_check_with(err, diagnostics, snapshot_targets, snapshot_before,
                                         check_command_text(CommandKind::New));
            };

            bool has_selection_file = false;
            if (!project_root.empty())
            {
                bool is_ok = false;
                has_selection_file = is_regular_file(project_root / "plugins.json", is_ok) && is_ok;
            }

            if (has_selection && has_selection_file)
            {
                DiagnosticSet diagnostics;
                diagnostics.add(make_diagnostic(
                    ProjectCategory::SelectionSourceConflict, to_utf8(project_root / "plugins.json"),
                    "选择来源只有一个：已存在的 plugins.json 或 --plugin/--empty",
                    request.is_empty ? "plugins.json 已存在又给了 --empty" : "plugins.json 已存在又给了 --plugin",
                    to_utf8(project_root / "plugins.json"),
                    "去掉 --plugin/--empty 直接读既有选择，或先删除 plugins.json 再给出选择"));
                return finish_early(diagnostics);
            }

            if (!has_selection && !has_selection_file)
            {
                // 非交互且没有选择来源：不读 stdin、不等待，直接给可复制的两种形式
                if (!is_stdin_terminal())
                {
                    DiagnosticSet diagnostics;
                    diagnostics.add(make_diagnostic(
                        ProjectCategory::CliUsage, request.name, "给出插件选择来源",
                        "非交互环境既没有 --plugin 也没有 --empty，且项目内没有 plugins.json",
                        to_utf8(project_root),
                        "改用以下任一形式重跑：ID project new --name " + request.name + " --path " + request.path
                            + " --plugin IDJson 或 ID project new --name " + request.name + " --path " + request.path
                            + " --empty"));
                    return finish_early(diagnostics);
                }

                // 交互式选择：列出可发现插件，结果转成与 --plugin 相同的 PluginSpec 序列
                const PluginCatalog catalog = discover_plugins(engine_root / to_native(kPluginRoot));
                const InteractiveResult interactive = run_interactive_selection(catalog);
                if (!interactive.is_ok || interactive.is_cancelled)
                {
                    DiagnosticSet diagnostics;
                    diagnostics.add(make_diagnostic(
                        ProjectCategory::CliUsage, request.name, "完成交互式选择",
                        interactive.reason.empty() ? "交互式选择未完成" : interactive.reason, to_utf8(project_root),
                        "改用可脚本化的形式重跑：ID project new --name " + request.name + " --path " + request.path
                            + " --plugin <插件名> 或 --empty"));
                    return finish_early(diagnostics);
                }

                specs         = interactive.plugins;
                has_selection = true;
            }

            SelectionFile selection;
            if (has_selection)
            {
                // `linkage` 缺省按清单 requirements 展开：CLI 只表达"选哪个插件"
                const PluginCatalog      catalog = discover_plugins(engine_root / to_native(kPluginRoot));
                const std::vector<PluginSpec> resolved_specs = fill_linkage_defaults(specs, catalog);
                selection = make_selection_file(request.name, resolved_specs);
            }

            return run_write_command(request, context, engine_root, has_selection, selection, next_argv, out, err);
        }
    } // namespace

    std::filesystem::path resolve_engine_root(std::string_view override_root)
    {
        return resolve_engine_root_detail(override_root).root;
    }

    int run_command_line(std::span<const std::string> arguments, const RunnerContext& context)
    {
        std::ostream& out = context.io.out != nullptr ? *context.io.out : std::cout;
        std::ostream& err = context.io.err != nullptr ? *context.io.err : std::cerr;

        if (!context.is_injection_initialized)
        {
            const char* const inject = std::getenv("ID_TEST_INJECT");
            init_injection(inject != nullptr ? std::string_view(inject) : std::string_view());
        }

        const CommandParse parsed = parse_command_line(arguments);
        if (!parsed.is_ok)
        {
            // 解析失败不初始化日志器：诊断只写 stderr，不创建目录与文件
            print_diagnostics(err, parsed.diagnostics);
            err << project_usage();
            return select_exit_code(parsed.diagnostics.span(), false);
        }

        // 日志器生命周期：解析成功之后、任何阶段开始之前
        LifeCycle::OnInit::IDLog_init();
        if (!LifeCycle::OnInit::IDProject_logger_init())
        {
            DiagnosticSet diagnostics;
            diagnostics.add(make_diagnostic(ProjectCategory::WriteFailed, "IDProject", "工具日志器就绪",
                                            "IDProject_logger_init 返回 false", {},
                                            "检查运行环境的控制台输出是否可用后重跑"));
            print_diagnostics(err, diagnostics);
            LifeCycle::OnShutdown::IDProject_logger_shutdown();
            return select_exit_code(diagnostics.span(), false);
        }

        IDPROJECT_LOG_TRACE("run_command_line: 开始执行 ID project 命令");

        const EngineRootResolution engine = resolve_engine_root_detail(to_utf8(context.engine_root));
        if (engine.root.empty())
        {
            DiagnosticSet diagnostics;
            std::string   tried;
            for (const std::string& item : engine.tried)
            {
                if (!tried.empty())
                {
                    tried.append("；");
                }
                tried.append(item);
            }
            if (tried.empty())
            {
                tried = "没有可用的候选来源";
            }

            diagnostics.add(make_diagnostic(
                ProjectCategory::ProjectPathInvalid, "engine_root",
                "引擎根含 ID/，且含 IDPlugin/ 或 ID/project.json 本机登记", "已尝试的来源：" + tried, "engine_root",
                "在工程根下运行，或用 ID_ENGINE_ROOT 指定含 ID/（装配还需要 IDPlugin/）的目录"));
            print_diagnostics(err, diagnostics);
            LifeCycle::OnShutdown::IDProject_logger_shutdown();
            return select_exit_code(diagnostics.span(), false);
        }

        // 进程内驱动（Benchmark）可能只给 engine_root：登记与插件源根按同一规则补齐，不要求调用方重复填
        RunnerContext effective = context;
        effective.engine_root   = engine.root;
        if (effective.registry_path.empty())
        {
            effective.registry_path = engine.root / "ID" / "project.json";
        }
        if (effective.plugin_root.empty())
        {
            effective.plugin_root = engine.root / "IDPlugin";
        }

        // 收敛命令：本次实际参数原样落进 intent，诊断可据此给出可直接复制的续跑命令（不含程序名与 `project`）
        const std::vector<std::string> next_argv(arguments.begin(), arguments.end());

        int exit_code = kExitSuccess;
        switch (parsed.request.command)
        {
            case Subcommand::New:
                exit_code = run_new_command(parsed.request, effective, engine.root, next_argv, out, err);
                break;
            case Subcommand::Update:
            case Subcommand::Clean:
                exit_code = run_write_command(parsed.request, effective, engine.root, false, SelectionFile{},
                                              next_argv, out, err);
                break;
            case Subcommand::Build:
                exit_code = run_build_command(parsed.request, effective, engine.root, out, err);
                break;
            case Subcommand::Recover:
                exit_code = run_recover_command(parsed.request, effective, engine.root, out, err);
                break;
            case Subcommand::List:
                exit_code = run_list_command(effective, out, err);
                break;
        }

        IDPROJECT_LOG_TRACE("run_command_line: 命令结束，退出码 {}", exit_code);
        LifeCycle::OnShutdown::IDProject_logger_shutdown();
        return exit_code;
    }
} // namespace ID::Project
