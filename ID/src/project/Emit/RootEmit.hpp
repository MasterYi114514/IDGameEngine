#pragma once

#include <string_view>

#include "cmake/Common/Diagnostic.hpp"
#include "project/Emit/GeneratedTexts.hpp"
#include "project/Resolve/Resolution.hpp"

/**
 *  根 `CMakeLists.txt` 的模型装配与三段文本产出（docs/id/08 §8.4、spec §11.2）。
 *
 *  三段文本由同一份骨架顺序产出：`HeaderOnly`（第 1–2 项）、`ConfigurationOnly`（第 3–4 项）、
 *  `TargetsOnly`（第 5–13 项），依次拼接就是整文件正文，不出现第二份顺序实现。
 *
 *  取值来源：项目名、`resolution.selection.direct`（直接选择的插件目标）与 `naming`；本文件不读清单、
 *  不读记录、不按插件名特判。
 *
 *  关键不变量：
 *  1. 首行是 `cmake_minimum_required(VERSION 4.1.0)`，不写生成头注释。
 *  2. `ID_OUTPUT_ROOT` 只在 `output-dirs` 片段定义一次（父作用域），`plugin-targets` 片段只拼 `/debug`、
 *     `/release`，不嵌入 `$<CONFIG>`，也不派生配置名变量。
 *  3. `target_link_libraries` 位于 `add_subdirectory` 之前；空选择时整块省略，不写空块。
 *  4. `BUILD_RPATH` 只给根可执行目标（同目录共享库的运行期搜索）。
 *  5. 模型装配后先 `ID::CMake::validate`，失败时不产出任何文本，只回报原始校验报告。
 */

namespace ID::Project
{
    /**
     *  根 `CMakeLists.txt` 的三段文本与校验报告；三段同属一个文件。
     */
    struct RootEmitResult
    {
        GeneratedText           header         = { };   // 第 1–2 项
        GeneratedText           output_dirs    = { };   // output-dirs 片段体
        GeneratedText           plugin_targets = { };   // plugin-targets 片段体
        CMake::ValidationReport report         = { };   // 根模型的校验报告；is_valid 为假时文本为空
    };

    /**
     *  @brief 装配根目录模型并产出三段文本
     *  @param assembly 组装结果（只读；取直接选择项与命名计划）
     *  @param project_name 项目名（同时是 CMake 工程名与可执行目标名）
     *  @return 三段文本与校验报告；校验失败时三段文本为空
     *  @note 不写盘、不读磁盘；相同输入产出逐字节相同的文本
     */
    RootEmitResult emit_root(const ResolvedAssembly& assembly, std::string_view project_name);
} // namespace ID::Project
