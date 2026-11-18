#include "project/Emit/PluginEmit.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cmake/Common/Assert.hpp"
#include "cmake/Directory/Directory.hpp"
#include "cmake/Directory/DirectoryEmitter.hpp"
#include "cmake/Project/Project.hpp"
#include "cmake/Target/Scope.hpp"
#include "cmake/Target/Target.hpp"
#include "cmake/Target/TargetType.hpp"
#include "cmake/Value/Argument.hpp"
#include "project/Base/Text.hpp"

/**
 *  插件构建描述的模型装配：一个插件一份整文件，全部取值来自 `ResolvedAssembly`。
 *
 *  三个"不猜"：
 *  1. 不猜 `SYSTEM`：include 作用域只取 `include_roots[].is_system`，不看路径是否落在 vendor 目录下；
 *     路径前缀剥离只用于把"相对项目根"的路径改写成 `${CMAKE_CURRENT_SOURCE_DIR}/...`（插件文件在插件根）。
 *  2. 不猜形态：`STATIC` / `SHARED` 只取 `linkage`，不生成形态 `option`，也不推断。
 *  3. 不猜链接：无条件边与条件边都由 `Resolution.edges` 决定，选项名到选项变量的映射只用
 *     `ID<TAG>_<OPTION>` 这一条生成期命名规则（`skills/id-plugin` §4.3）。
 *
 *  `POSITION_INDEPENDENT_CODE` 只按有效链接边给"会被链进某个 `SHARED` 目标"的静态目标：从 `SHARED`
 *  形态的插件出发，沿"下游链接上游"的边向上游传播（上游对象会进入下游产物），不按名字或路径猜。
 */

namespace ID::Project
{
    namespace
    {
        // 插件根相对工程根的路径段：编译面事实里的路径相对项目根，插件文件在插件根内
        constexpr std::string_view kPluginRootSegment = "IDPlugin/";

        // 每个插件的构建描述文件名
        constexpr std::string_view kCMakeFileName = "CMakeLists.txt";

        // 同目录共享库的运行期搜索路径（spec §11.6 第 1 条）：字面 $ORIGIN，不按平台分第二套分支
        constexpr std::string_view kOriginRpath = "$ORIGIN";

        // 目标声明前的拒绝守卫说明串：命中即 configure 期失败，不静默复用同名目标
        constexpr std::string_view kGuardLead = "目标 ";
        constexpr std::string_view kGuardTail =
            " 已存在：本文件由 ID 生成，同名目标只能在生成的构建描述中定义";

        // 选项说明串缺失（清单该位为空串）的限制代号：稳定标识，脚本可断言
        constexpr std::string_view kDescriptionMissingCode = "option_description_missing";

        CMake::Argument make_debug_output_directory()
        {
            return CMake::Argument::quoted_expand("${ID_OUTPUT_ROOT}/debug");
        }

        CMake::Argument make_release_output_directory()
        {
            return CMake::Argument::quoted_expand("${ID_OUTPUT_ROOT}/release");
        }

        // 插件文件相对项目根的路径：IDPlugin/<Plugin>/CMakeLists.txt
        std::string make_file_path(std::string_view plugin)
        {
            std::string path(kPluginRootSegment);
            path.append(plugin);
            path.push_back('/');
            path.append(kCMakeFileName);
            return path;
        }

        // 把"相对项目根"的路径改写成"相对插件根"：只剥离 "IDPlugin/<Plugin>/" 段，不参与 is_system 判定
        std::string plugin_relative_path(std::string_view path, std::string_view plugin)
        {
            std::string prefix(kPluginRootSegment);
            prefix.append(plugin);
            prefix.push_back('/');

            if (starts_with(path, prefix) && path.size() > prefix.size())
            {
                return std::string(path.substr(prefix.size()));
            }
            // 形态不符合预期时原样承载，让 configure 期报真实错误，不在生成期猜测或补默认值
            return std::string(path);
        }

        std::string make_include_path(std::string_view relative_dir)
        {
            std::string path("${CMAKE_CURRENT_SOURCE_DIR}/");
            path.append(relative_dir);
            return path;
        }

        const EffectivePlugin* find_effective(const std::vector<EffectivePlugin>& plugins, std::string_view name) noexcept
        {
            for (const EffectivePlugin& plugin : plugins)
            {
                if (std::string_view(plugin.name) == name)
                {
                    return &plugin;
                }
            }
            return nullptr;
        }

        const PluginCompileFacts* find_compile_facts(const std::vector<PluginCompileFacts>& facts,
                                                    std::string_view plugin) noexcept
        {
            for (const PluginCompileFacts& entry : facts)
            {
                if (std::string_view(entry.plugin) == plugin)
                {
                    return &entry;
                }
            }
            return nullptr;
        }

        /**
         *  一个选项的生成期形态：选项名、选项变量、要定义的宏、清单说明串与清单缺省值。
         *  `define` 为空表示该选项不产生宏（清单没有 `defines`）；`description` 为空表示清单该位
         *  没有说明串，装配期为它写占位串并登记一条 `option_description_missing`。
         */
        struct OptionPlan
        {
            std::string option        = { };   // 选项名，不带前缀
            std::string var           = { };   // ID<TAG>_<OPTION>，进 option() 与 if() 条件
            std::string define        = { };   // 宏名；空表示不产生宏
            std::string description   = { };   // 清单 options[].description 原文；空表示该位缺失
            bool        is_default_on = false; // 清单 default 的布尔解释
        };

        // 三个平行列表同长度同序（`Naming.hpp` 的模型口径：option_vars / option_defines /
        // option_descriptions 都按选项名排序），因此一律按**同名下标**配对：不按宏名排序，
        // 也不跳过没有 defines 的选项——"只收有 defines 的项"会让第一个无宏选项之后整体错位
        std::vector<OptionPlan> plan_options(const PluginNaming& naming, const EffectivePlugin* effective)
        {
            ID_ASSERT(naming.option_defines.size() == naming.option_vars.size());
            ID_ASSERT(naming.option_descriptions.size() == naming.option_vars.size());

            std::vector<OptionPlan> plans;
            plans.reserve(naming.option_vars.size());

            const std::string prefix = "ID" + naming.tag + "_";
            for (std::size_t index = 0; index < naming.option_vars.size(); ++index)
            {
                OptionPlan plan;
                plan.var         = naming.option_vars[index];
                plan.option      = starts_with(plan.var, prefix) ? plan.var.substr(prefix.size()) : plan.var;
                plan.define      = naming.option_defines[index];
                plan.description = naming.option_descriptions[index];
                if (effective != nullptr)
                {
                    (void)find_option_value(*effective, plan.option, plan.is_default_on);
                }
                plans.push_back(std::move(plan));
            }
            return plans;
        }

        // 条件边的条件表达式：控制选项的变量名按字节序 OR 合并成单个表达式，不拆成多个 if
        std::string make_condition_text(const PluginNaming& naming, const std::vector<std::string>& controlling_options)
        {
            std::string condition;
            for (const std::string& option : controlling_options)
            {
                if (!condition.empty())
                {
                    condition.append(" OR ");
                }
                condition.append("ID");
                condition.append(naming.tag);
                condition.push_back('_');
                condition.append(option);
            }
            return condition;
        }

        // 该插件是否会被链接进某个 SHARED 目标的产物：从 SHARED 插件出发沿"下游链接上游"传播
        bool is_linked_into_shared(const Resolution& resolution, std::string_view plugin)
        {
            std::vector<std::string> reached;
            for (const EffectivePlugin& candidate : resolution.selection.all)
            {
                if (candidate.linkage == Linkage::Dynamic)
                {
                    reached.push_back(candidate.name);
                }
            }

            auto contains = [&reached](std::string_view name) noexcept
            {
                for (const std::string& entry : reached)
                {
                    if (std::string_view(entry) == name)
                    {
                        return true;
                    }
                }
                return false;
            };

            if (contains(plugin))
            {
                return true;
            }

            // 上游对象进入下游产物，因此"下游可达"沿边反向传播；集合很小，重复扫描到稳定即可
            bool has_changed = true;
            while (has_changed)
            {
                has_changed = false;
                for (const LinkEdge& edge : resolution.edges)
                {
                    if (!contains(edge.from) || contains(edge.to))
                    {
                        continue;
                    }
                    reached.push_back(edge.to);
                    has_changed = true;
                }
            }
            return contains(plugin);
        }

        // 该插件的边：条件边与无条件边分别取，均保持 (from, to) 的字节序
        std::vector<const LinkEdge*> select_edges(const Resolution& resolution, std::string_view plugin, bool is_conditional)
        {
            std::vector<const LinkEdge*> edges;
            for (const LinkEdge& edge : resolution.edges)
            {
                if (std::string_view(edge.from) != plugin || edge.is_conditional != is_conditional)
                {
                    continue;
                }
                edges.push_back(&edge);
            }
            return edges;
        }

        // 目标声明前的拒绝守卫：形态固定为 message(FATAL_ERROR)，说明串单行、含目标名
        std::string make_guard_message(std::string_view target)
        {
            std::string message(kGuardLead);
            message.append(target);
            message.append(kGuardTail);
            return message;
        }

        // 说明串缺失（清单该位为空串）时的占位串：仍写确定性文本，不写空说明串
        // （空说明串在 configure 的选项列表里不可读）。缺失本身由调用方按 option_description_missing 登记
        std::string make_missing_description(std::string_view plugin, std::string_view option_var)
        {
            std::string description(plugin);
            description.append(" option ");
            description.append(option_var);
            return description;
        }

        // 一条说明串缺失的限制：写明插件与选项名（两者都进 message，便于直接定位清单条目）
        EmitLimitation make_description_limitation(std::string_view plugin, std::string_view option)
        {
            EmitLimitation limitation;
            limitation.code    = std::string(kDescriptionMissingCode);
            limitation.message = "插件 ";
            limitation.message.append(plugin);
            limitation.message.append(" 的选项 ");
            limitation.message.append(option);
            limitation.message.append(
                " 在清单 options[].description 中没有说明串：本次 option() 的说明位写占位串"
                "「<插件名> option <选项变量>」而不写空串，清单补上说明串后消失"
                "（spec §11.4 第 2 条要求 option() 取清单 description）；该限制不改变退出码，"
                "也不改变本次生成结果是否可用");
            return limitation;
        }

        /**
         *  选项宏分支的排版自检：分支顺序必须等于 `option_vars` 的选项名序，不是宏名序。
         *  `add_definitions` 按选项顺序逐条追加分支，这里读回模型比对；任何"按宏名排序"或
         *  "跳项后错位"的实现都会在出文本之前终止，不会静默产出顺序错误的编译宏块。
         */
        void assert_macro_branch_order(const std::vector<OptionPlan>& options, const CMake::Target& target)
        {
            std::size_t expected = 0;
            for (const OptionPlan& plan : options)
            {
                if (plan.define.empty())
                {
                    continue;
                }
                ID_ASSERT(expected < target.branch_count());
                const std::string_view condition = target.branch_at(expected).condition.get_text();
                ID_ASSERT(condition == std::string_view(plan.var));
                ++expected;
            }
            ID_ASSERT(expected == target.branch_count());
        }

        // 第 5 项：收源通配目录逐项登记（多目录合成一条命令由 emitter 负责），按字节序
        void add_source_glob_dirs(const PluginCompileFacts& facts, CMake::Target& target)
        {
            for (const std::string& directory : facts.source_dirs)
            {
                target.add_source_glob_dir(plugin_relative_path(directory, facts.plugin));
            }
        }

        // 第 7 项：形态宏（按 linkage 单条）与选项宏分支（按选项名排序，互斥 if/else）
        void add_definitions(const PluginNaming& naming, bool is_dynamic, const std::vector<OptionPlan>& options,
                             CMake::Target& target)
        {
            if (is_dynamic)
            {
                target.add_compile_definition(CMake::Scope::Private, CMake::Argument::bare(naming.export_macro));
            }
            else
            {
                target.add_compile_definition(CMake::Scope::Public, CMake::Argument::bare(naming.static_macro));
            }

            for (const OptionPlan& plan : options)
            {
                if (plan.define.empty())
                {
                    continue;
                }
                target.add_branch(
                    CMake::Argument::bare(plan.var),
                    CMake::BranchStatement{CMake::BranchPayload::CompileDefinition, CMake::Scope::Private,
                                           CMake::Argument::bare(plan.define + "=1")},
                    CMake::BranchStatement{CMake::BranchPayload::CompileDefinition, CMake::Scope::Private,
                                           CMake::Argument::bare(plan.define + "=0")});
            }
        }

        // 第 10 项：include 根按 is_system 决定作用域，另加一条私有 src
        void add_include_roots(const PluginCompileFacts& facts, CMake::Target& target)
        {
            for (const IncludeRoot& include_root : facts.include_roots)
            {
                // 作用域的唯一判据是模型取值：is_system 为真写 SYSTEM PUBLIC，为假写 PUBLIC
                target.add_include_directory(CMake::Scope::Public,
                                             CMake::Argument::quoted_expand(
                                                 make_include_path(plugin_relative_path(include_root.path, facts.plugin))),
                                             std::nullopt,
                                             include_root.is_system);
            }
            target.add_include_directory(CMake::Scope::Private,
                                        CMake::Argument::quoted_expand(make_include_path("src")));
        }

        // 第 12 项：Common 为 PUBLIC，无条件上游与 vendor 目标为 PRIVATE，条件边单独成条件块
        void add_links(const ResolvedAssembly& assembly, const PluginNaming& naming, const PluginCompileFacts& facts,
                       CMake::Target& target)
        {
            target.add_link_library(CMake::Scope::Public, CMake::Argument::bare(assembly.naming.common_target));

            for (const LinkEdge* edge : select_edges(assembly.resolution, naming.plugin, false))
            {
                target.add_link_library(CMake::Scope::Private, CMake::Argument::bare(edge->to));
            }

            for (const LinkEdge* edge : select_edges(assembly.resolution, naming.plugin, true))
            {
                target.add_link_library(CMake::Scope::Private, CMake::Argument::bare(edge->to),
                                        CMake::Argument::verbatim(make_condition_text(naming, edge->controlling_options)));
            }

            // vendor 目标由聚合文件接入，本文件只链接；target 为空的条目在模型里已无链接项
            for (const VendorLink& link : facts.vendor_links)
            {
                target.add_link_library(CMake::Scope::Private, CMake::Argument::bare(link.target));
            }
        }

        // 本工程模型只声明一个目标：链接项里的 Common、上游插件与 vendor 目标都在别的文件里声明
        void register_external_targets(const ResolvedAssembly& assembly, const PluginCompileFacts& facts,
                                       CMake::Project& project)
        {
            project.add_external_target(CMake::Argument::bare(assembly.naming.common_target));

            for (const PluginNaming& naming : assembly.naming.plugins)
            {
                project.add_external_target(CMake::Argument::bare(naming.target));
            }

            // 边集合是链接关系的唯一来源：即使上游目标名不在命名计划里也照样登记，避免把
            // validate 的 LinkTargetNotFound 变成"名字没登记"的假故障
            for (const LinkEdge& edge : assembly.resolution.edges)
            {
                if (std::string_view(edge.from) == facts.plugin)
                {
                    project.add_external_target(CMake::Argument::bare(edge.to));
                }
            }

            for (const VendorLink& link : facts.vendor_links)
            {
                project.add_external_target(CMake::Argument::bare(link.target));
            }
        }

        // 一个插件文件：模型装配 → validate → emit_directory（校验失败时不产出文本）
        PluginFileResult emit_plugin(const ResolvedAssembly& assembly, const PluginNaming& naming)
        {
            PluginFileResult result;
            result.plugin = naming.plugin;
            result.text.target_relative = make_file_path(naming.plugin);

            if (!assembly.naming.is_valid)
            {
                // 命名计划无效时没有可信的目标名与宏名：不产出文本，并让上层按 cmake_name_conflict 拒绝生成
                result.report.is_valid = false;
                result.report.diagnostics.push_back(CMake::Diagnostic{
                    CMake::CMakeError::InvalidName, naming.plugin, "有效的命名计划（NamingPlan.is_valid 为真）",
                    "命名计划无效：目标名与派生宏名不可用", std::string("NamingPlan"),
                    "先按 cmake_name_conflict 的定位修好命名冲突，再重跑 P1 生成"});
                return result;
            }

            const EffectivePlugin* effective = find_effective(assembly.resolution.selection.all, naming.plugin);
            const PluginCompileFacts* facts  = find_compile_facts(assembly.resolution.compile_facts, naming.plugin);
            if (facts == nullptr)
            {
                // 编译面事实缺失属实现缺陷（resolve_closure 为闭包内每个插件都产出事实），不做静默兜底
                result.report.is_valid = false;
                result.report.diagnostics.push_back(CMake::Diagnostic{
                    CMake::CMakeError::MissingSourceGlobDir, naming.plugin,
                    "Resolution.compile_facts 中有该插件的编译面事实",
                    "compile_facts 缺少该插件（闭包内每个插件都应有一条）", std::string("Resolution.compile_facts"),
                    "重跑 P1 解析，确认 resolve_closure 为闭包内每个插件都产出了编译面事实"});
                return result;
            }

            const bool       is_dynamic = effective != nullptr && effective->linkage == Linkage::Dynamic;
            const bool       is_pic     = !is_dynamic && is_linked_into_shared(assembly.resolution, naming.plugin);
            const std::vector<OptionPlan> options = plan_options(naming, effective);

            CMake::Project    project(naming.plugin);
            CMake::Directory& root = project.root();

            // 第 3 项：每个选项一条 option()，按选项名排序（option_vars 已按选项名排序）。
            // 说明串取清单 `options[].description` 的**同下标**位（D11）：转义由 `Argument::quoted` 的
            // 渲染完成（`\`、`"`、`$` 各加一个反斜杠），此处不重复转义、也不回读清单；
            // 该位为空串时写占位串并登记一条 `option_description_missing`，不静默留空
            for (const OptionPlan& plan : options)
            {
                const bool        is_missing  = plan.description.empty();
                const std::string description = is_missing ? make_missing_description(naming.plugin, plan.var)
                                                           : plan.description;
                if (is_missing)
                {
                    result.limitations.push_back(make_description_limitation(naming.plugin, plan.option));
                }
                root.add_option(CMake::Argument::bare(plan.var), plan.is_default_on,
                                CMake::Argument::quoted(description));
            }

            // 第 5–6 项：收源通配、目标声明前的拒绝守卫与目标声明
            CMake::Target& target = root.create_target(
                naming.target,
                is_dynamic ? CMake::TargetType::SharedLibrary : CMake::TargetType::StaticLibrary);
            add_source_glob_dirs(*facts, target);
            target.set_guard(CMake::Argument::bare(naming.target),
                             CMake::Argument::quoted(make_guard_message(naming.target)));

            // 第 7–8 项：形态宏与选项宏分支、编译特性；宏分支顺序在出文本前自检一次
            add_definitions(naming, is_dynamic, options, target);
            assert_macro_branch_order(options, target);
            target.set_compile_standard(CMake::Argument::bare(CMake::kSupportedCompileStandard));

            // 第 9 项：按配置输出目录、PIC（仅会被链进 SHARED 的静态目标）、BUILD_RPATH（仅 SHARED）
            target.set_config_output_directories(make_debug_output_directory(), make_release_output_directory());
            if (is_pic)
            {
                target.set_position_independent_code(true);
            }
            if (is_dynamic)
            {
                target.set_build_rpath(std::vector<std::string>{std::string(kOriginRpath)});
            }

            // 第 10 项与第 12 项
            add_include_roots(*facts, target);
            add_links(assembly, naming, *facts, target);

            register_external_targets(assembly, *facts, project);

            result.report = CMake::validate(project);
            if (!result.report.is_valid)
            {
                return result;
            }

            result.text.text = CMake::emit_directory(root, CMake::EmitScope::WholeFile);
            return result;
        }
    } // namespace

    PluginEmitResult emit_plugins(const ResolvedAssembly& assembly)
    {
        PluginEmitResult result;
        result.files.reserve(assembly.naming.plugins.size());

        for (const PluginNaming& naming : assembly.naming.plugins)
        {
            result.files.push_back(emit_plugin(assembly, naming));
        }
        return result;
    }
} // namespace ID::Project
