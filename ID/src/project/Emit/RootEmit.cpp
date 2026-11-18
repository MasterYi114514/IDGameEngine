#include "project/Emit/RootEmit.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cmake/Directory/Directory.hpp"
#include "cmake/Directory/DirectoryEmitter.hpp"
#include "cmake/File/FragmentLayout.hpp"
#include "cmake/Project/Project.hpp"
#include "cmake/Target/Scope.hpp"
#include "cmake/Target/Target.hpp"
#include "cmake/Target/TargetType.hpp"
#include "cmake/Value/Argument.hpp"

/**
 *  根 `CMakeLists.txt` 的模型装配：三段 scope 共用同一份骨架顺序实现。
 *
 *  装配顺序（docs/id/08 §8.4.1）：配置变量 `ID_OUTPUT_ROOT` → 可执行目标（收源目录、配置宏分支、
 *  编译特性、按配置输出目录、`BUILD_RPATH`、私有 include、直接选择项的链接）→ `add_subdirectory(IDPlugin)`。
 *
 *  取值口径：
 *  1. 输出目录只经 `${ID_OUTPUT_ROOT}` 派生，属性值为 `${ID_OUTPUT_ROOT}/debug` 与 `${ID_OUTPUT_ROOT}/release`；
 *     变量只在本文件的 `output-dirs` 片段定义一次，且只由 `${CMAKE_SOURCE_DIR}` 与两个身份段变量组成。
 *  2. 链接项只取 `selection.direct`（用户直接选择的插件目标），上游由静态库的 `PRIVATE` 链接自动进入最终闭包；
 *     空选择时一条链接项都没有，`target_link_libraries` 整块按需省略（模型没有条目，emitter 不写空块）。
 *  3. 保留符号 `ID_DEBUG` / `ID_RELEASE` 是生成器自带的配置宏（`Naming` 的保留符号表），按
 *     `CMAKE_BUILD_TYPE` 互斥分支写出，不由清单推导。
 *  4. 模型装配后先 `ID::CMake::validate`：失败时不产出任何文本，避免触发 emitter 的前置断言。
 */

namespace ID::Project
{
    namespace
    {
        // 三段同属一个文件：相对项目根的固定路径
        constexpr std::string_view kRootFilePath = "CMakeLists.txt";

        // 第 13 项的唯一子目录：其 CMakeLists.txt 由 AggregateEmit 产出，本模型只写 add_subdirectory
        constexpr std::string_view kAggregateDirectory = "IDPlugin";

        // output-dirs 片段的空值拒绝说明串：绕过 build 直接 configure 时的兜底（spec §11.2）
        constexpr std::string_view kOutputRootGuardMessage =
            "ID_OUTPUT_ROOT 缺少身份段：configure 时必须传入 -DID_BUILD_TOOLCHAIN 与 -DID_BUILD_ARCH";

        // 根可执行目标的同目录共享库运行期搜索路径（spec §11.6 第 1 条）
        constexpr std::string_view kOriginRpath = "$ORIGIN";

        CMake::Argument make_debug_output_directory()
        {
            return CMake::Argument::quoted_expand("${ID_OUTPUT_ROOT}/debug");
        }

        CMake::Argument make_release_output_directory()
        {
            return CMake::Argument::quoted_expand("${ID_OUTPUT_ROOT}/release");
        }

        // 一份生成文本：路径、片段身份（空表示整文件）与正文；摘要由计划阶段按身份计算
        GeneratedText make_text(std::string_view target_relative, std::string_view fragment, std::string text)
        {
            GeneratedText result;
            result.target_relative = std::string(target_relative);
            result.fragment        = std::string(fragment);
            result.text            = std::move(text);
            return result;
        }
    } // namespace

    RootEmitResult emit_root(const ResolvedAssembly& assembly, std::string_view project_name)
    {
        RootEmitResult result;

        CMake::Project   project(project_name);
        CMake::Directory& root = project.root();

        // 第 4 项：output-dirs 片段的唯一职责——先拒绝空身份段，再定义派生输出根（父作用域）
        root.add_configuration_variable(
            CMake::Argument::bare("ID_OUTPUT_ROOT"),
            CMake::Argument::quoted_expand("${CMAKE_SOURCE_DIR}/bin/${ID_BUILD_TOOLCHAIN}/${ID_BUILD_ARCH}"),
            CMake::Argument::verbatim("NOT ID_BUILD_TOOLCHAIN OR NOT ID_BUILD_ARCH"),
            CMake::Argument::quoted(kOutputRootGuardMessage));

        // 第 5–6 项：可执行目标。项目名唯一性在 P1 已判定，因此不写 TARGET 守卫
        CMake::Target& target = root.create_target(project_name, CMake::TargetType::Executable);
        target.add_source_glob_dir(CMake::kSourceGlobDirectory);
        target.set_compile_standard(CMake::Argument::bare(CMake::kSupportedCompileStandard));

        // 第 7 项：配置宏按 CMAKE_BUILD_TYPE 互斥分支；两个取值都是生成器自带宏
        target.add_branch(CMake::Argument::verbatim("CMAKE_BUILD_TYPE STREQUAL \"Debug\""),
                          CMake::BranchStatement{CMake::BranchPayload::CompileDefinition, CMake::Scope::Private,
                                                 CMake::Argument::bare("ID_DEBUG")},
                          CMake::BranchStatement{CMake::BranchPayload::CompileDefinition, CMake::Scope::Private,
                                                 CMake::Argument::bare("ID_RELEASE")});

        // 第 9 项：按配置的输出目录与同目录运行期搜索路径；不写 $<CONFIG>、不派生配置名变量
        target.set_config_output_directories(make_debug_output_directory(), make_release_output_directory());
        target.set_build_rpath(std::vector<std::string>{std::string(kOriginRpath)});

        // 第 10 项：私有 include 根（源目录本身；root 的收源目录固定为 src）
        target.add_include_directory(CMake::Scope::Private,
                                    CMake::Argument::quoted_expand("${CMAKE_CURRENT_SOURCE_DIR}/src"));

        // 第 12 项：只链接直接选择的插件目标，按插件名排序（selection.direct 已按名字升序）
        for (const EffectivePlugin& plugin : assembly.resolution.selection.direct)
        {
            target.add_link_library(CMake::Scope::Private, CMake::Argument::bare(plugin.name));
            // 插件目标由聚合文件声明：本工程模型只登记它可被链接，名字解析交给 configure 期
            project.add_external_target(CMake::Argument::bare(plugin.name));
        }

        // 第 13 项：只写 add_subdirectory，不建模子目录对象（其文件由 AggregateEmit 产出）
        root.create_referenced_subdirectory(kAggregateDirectory);

        result.report = CMake::validate(project);
        if (!result.report.is_valid)
        {
            return result;
        }

        result.header         = make_text(kRootFilePath, std::string_view{},
                                          CMake::emit_directory(root, CMake::EmitScope::HeaderOnly));
        result.output_dirs    = make_text(kRootFilePath, CMake::kFragmentOutputDirs,
                                          CMake::emit_directory(root, CMake::EmitScope::ConfigurationOnly));
        result.plugin_targets = make_text(kRootFilePath, CMake::kFragmentPluginTargets,
                                          CMake::emit_directory(root, CMake::EmitScope::TargetsOnly));
        return result;
    }
} // namespace ID::Project
