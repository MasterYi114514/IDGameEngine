#include "project/Emit/GeneratedTexts.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "project/Base/Digest.hpp"
#include "project/Emit/AggregateEmit.hpp"
#include "project/Emit/PluginEmit.hpp"
#include "project/Emit/RootEmit.hpp"
#include "project/Emit/SeedEmit.hpp"

/**
 *  装配串联：四类文本的产出顺序、排序、摘要回填与生成期限制（docs/id/08 §8.9）。
 *
 *  顺序固定为「根三段 → 每个插件 → 聚合文件 → seed」，与 `GeneratedTexts` 的字段顺序无关：
 *  诊断按同一顺序追加，因此同一输入得到同一条诊断序列。
 *
 *  失败语义（docs/id/08 §8.8）：`ID::CMake` 的诊断在这里翻译成项目类别，装配失败时不产出任何文本，
 *  调用方据 `is_valid` 决定不生成计划。`limitations` 与失败无关：它不进 `DiagnosticSet`、不改退出码、
 *  不改变 `is_valid`，只在成功摘要与 `--check` 输出里逐条打印。
 *
 *  摘要分工：只回填整文件生成项；根三段与两个受控片段的 N 由计划阶段对写后字节计算
 *  （`ID::CMake::fragment_range_digest`），本单元不实现第二份片段摘要。
 */

namespace ID::Project
{
    namespace
    {
        // 三类整文件生成项的项目相对路径
        constexpr std::string_view kRootFilePath      = "CMakeLists.txt";
        constexpr std::string_view kAggregateFilePath = "IDPlugin/CMakeLists.txt";
        constexpr std::string_view kPluginRootSegment = "IDPlugin/";
        constexpr std::string_view kCMakeFileName     = "CMakeLists.txt";

        // 首条限制固定为 G8：工程级选项与宏不投影到生成结果（spec §14.1 G8、§11.4 末条）
        constexpr std::string_view kEngineOptionLimitationCode = "g8_engine_option_not_projected";

        EmitLimitation make_engine_option_limitation()
        {
            EmitLimitation limitation;
            limitation.code    = std::string(kEngineOptionLimitationCode);
            limitation.message = "生成的插件目标不投影任何工程级选项与宏：清单十项字段没有表达"
                                 "「插件消费工程级选项」的位置，生成器也不扫描源码猜这种依赖（spec §14.1 G8）；"
                                 "生成的插件目标只定义契约里的形态宏与选项宏，因此"
                                 "「项目生成的目标与工程内构建在 debug 行为上等价」不成立。"
                                 "该限制不改变退出码，也不改变本次生成结果是否可用";
            return limitation;
        }

        // ID::CMake 诊断 → 项目类别（docs/id/08 §8.8 的映射表；未列出的类别按"装配参数非法"归 write_failed）
        ProjectCategory map_category(CMake::CMakeError error) noexcept
        {
            switch (error)
            {
                case CMake::CMakeError::EmptyName:
                case CMake::CMakeError::InvalidName:
                case CMake::CMakeError::InvalidBareArgument:
                case CMake::CMakeError::InvalidTargetType:
                case CMake::CMakeError::DuplicateTarget:
                case CMake::CMakeError::DuplicateSubdirectory:
                case CMake::CMakeError::DuplicateDerivedVariable:
                case CMake::CMakeError::InvalidFormOptionTarget:
                case CMake::CMakeError::MissingImportLocation:
                case CMake::CMakeError::ImportTargetSetting:
                case CMake::CMakeError::ImportTargetScope:
                case CMake::CMakeError::InterfaceTargetSetting:
                case CMake::CMakeError::InterfaceTargetScope:
                case CMake::CMakeError::LinkTargetNotFound:
                    // 命名在 P1 已预检：走到这里属实现缺陷，仍按 cmake_name_conflict 给码 1
                    return ProjectCategory::CmakeNameConflict;

                case CMake::CMakeError::UnsupportedCompileStandard:
                case CMake::CMakeError::MissingSourceGlobDir:
                case CMake::CMakeError::InvalidSourceGlobDir:
                    // P1 已按 asset_missing / path_outside_project 拒绝，走到这里属实现缺陷
                    return ProjectCategory::ProjectConfigInvalid;

                case CMake::CMakeError::InvalidConfigOutput:
                case CMake::CMakeError::InvalidMacroBranch:
                case CMake::CMakeError::InvalidGuard:
                case CMake::CMakeError::InvalidBuildRpath:
                case CMake::CMakeError::VerbatimArgument:
                case CMake::CMakeError::DuplicateFragment:
                case CMake::CMakeError::PathEscapesProjectRoot:
                case CMake::CMakeError::TargetModified:
                case CMake::CMakeError::MissingFile:
                case CMake::CMakeError::MissingMarker:
                case CMake::CMakeError::AmbiguousMarker:
                case CMake::CMakeError::IoFailed:
                    return ProjectCategory::WriteFailed;
            }
            return ProjectCategory::WriteFailed;
        }

        // 一个模型的校验报告 → 项目诊断集合；返回该模型是否通过
        bool translate(const CMake::ValidationReport& report, std::string_view file_path, DiagnosticSet& diagnostics)
        {
            for (const CMake::Diagnostic& diagnostic : report.diagnostics)
            {
                std::string actual(diagnostic.actual);
                actual.append("；CMake 定位：");
                actual.append(diagnostic.location);

                diagnostics.add(ProjectDiagnostic{map_category(diagnostic.error),
                                                  diagnostic.subject,
                                                  diagnostic.expected,
                                                  std::move(actual),
                                                  std::string(file_path),
                                                  diagnostic.next_action});
            }
            return report.is_valid;
        }

        // 一个插件的构建描述路径：IDPlugin/<Plugin>/CMakeLists.txt
        std::string make_plugin_file_path(std::string_view plugin)
        {
            std::string path(kPluginRootSegment);
            path.append(plugin);
            path.push_back('/');
            path.append(kCMakeFileName);
            return path;
        }

        // 回填一份整文件生成项的摘要：只在资产计划里已有同路径的生成条目时覆盖
        void apply_one(const GeneratedText& text, AssetPlan& assets)
        {
            if (text.target_relative.empty() || !text.fragment.empty() || text.text.empty())
            {
                return;
            }

            const Digest digest = digest_text(text.text);
            for (AssetFile& file : assets.files)
            {
                if (file.stage != AssetStage::Generate || file.target_relative != text.target_relative)
                {
                    continue;
                }
                file.digest = digest;
            }
        }
    } // namespace

    GeneratedTexts emit_all(const ResolvedAssembly& assembly, std::string_view project_name)
    {
        GeneratedTexts texts;
        texts.limitations.push_back(make_engine_option_limitation());

        // 四类文本各自装配并校验；限制与校验无关，因此先收齐限制再判成败。
        // 限制顺序固定为「G8 → 各插件的限制（插件名序、选项名序）→ 聚合层的降级项」，
        // 同一输入得到同一条限制序列
        const RootEmitResult      root      = emit_root(assembly, project_name);
        const PluginEmitResult    plugins   = emit_plugins(assembly);
        const AggregateEmitResult aggregate = emit_aggregate(assembly);

        for (const PluginFileResult& file : plugins.files)
        {
            for (const EmitLimitation& limitation : file.limitations)
            {
                texts.limitations.push_back(limitation);
            }
        }
        for (const EmitLimitation& limitation : aggregate.limitations)
        {
            texts.limitations.push_back(limitation);
        }

        bool is_valid = translate(root.report, kRootFilePath, texts.diagnostics);
        for (const PluginFileResult& file : plugins.files)
        {
            const std::string file_path = make_plugin_file_path(file.plugin);
            is_valid = translate(file.report, file_path, texts.diagnostics) && is_valid;
        }
        is_valid = translate(aggregate.report, kAggregateFilePath, texts.diagnostics) && is_valid;

        if (!is_valid)
        {
            // 装配失败必须在任何写入之前发生：不产出文本，避免调用方拿到未校验的内容
            texts.is_valid = false;
            return texts;
        }

        texts.root_header         = root.header;
        texts.root_output_dirs    = root.output_dirs;
        texts.root_plugin_targets = root.plugin_targets;
        texts.aggregate           = aggregate.aggregate;

        texts.plugins.reserve(plugins.files.size());
        for (const PluginFileResult& file : plugins.files)
        {
            texts.plugins.push_back(file.text);
        }

        texts.seed     = emit_seed(project_name);
        texts.is_valid = true;
        return texts;
    }

    void apply_generated_digests(const GeneratedTexts& texts, AssetPlan& assets)
    {
        // 只回填整文件生成项：根三段与受控片段不进入资产计划，片段 N 由计划阶段按写后字节计算
        apply_one(texts.aggregate, assets);
        for (const GeneratedText& plugin : texts.plugins)
        {
            apply_one(plugin, assets);
        }
        apply_one(texts.seed, assets);
    }
} // namespace ID::Project
