#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "project/Base/Diagnostic.hpp"
#include "project/Base/Digest.hpp"
#include "project/Resolve/AssetPlan.hpp"
#include "project/Resolve/Resolution.hpp"

/**
 *  生成文本的载体与装配入口：根 `CMakeLists.txt` 三段、`IDPlugin/CMakeLists.txt`、每个插件的
 *  `CMakeLists.txt` 与 `src/main.cpp` 骨架，以及生成期行为限制（docs/id/08 §8.3、§8.9）。
 *
 *  职责边界：本单元**只产出文本与摘要**——不写盘、不判冲突、不决定写哪些文件（那是计划与执行阶段）。
 *  输入只取 `ResolvedAssembly`：编译面事实（`source_dirs`、`include_roots`、`vendor_links`）只经
 *  `Resolution.compile_facts` 取得，**不回头读** `plugin_config.json` / `plugins.json` / `resolved.json`，
 *  也不按路径前缀推断 `include_roots[].is_system`。
 *
 *  不变量：
 *  1. `GeneratedText.digest` 只覆盖 `fragment` 为空的**整文件生成项**（`IDPlugin/CMakeLists.txt`、
 *     每个插件的 `CMakeLists.txt`、`src/main.cpp`）。两个受控片段的 N 由计划阶段用
 *     `ID::CMake::fragment_range_digest` 对写后字节计算，本单元不产出片段摘要。
 *  2. 根 `CMakeLists.txt` 的三段文本同属一个文件，三段的 `digest` 全部为零值；整文件摘要由计划阶段
 *     对组合后的整文件字节计算，不在本单元拼装第二份整文件。
 *  3. `limitations` 不是失败：它不进入 `DiagnosticSet`、不改退出码、不改变 `is_valid`，但必须由编排层在
 *     成功摘要与 `--check` 输出里逐条打印。顺序固定：首条 `g8_engine_option_not_projected`，其后是各插件
 *     的限制（插件名序、选项名序，例如 `option_description_missing`），最后是聚合层的降级项。
 *  4. 相同输入产出逐字节相同的文本：不含时间戳、随机顺序与本机绝对路径（输出目录只用
 *     `${ID_OUTPUT_ROOT}` 派生）。
 *  5. 本单元不含插件名或选项名特判：选项效果只由 `defines` 与 `links_when_on` 表达，形态只由 `linkage` 决定。
 */

namespace ID::Project
{
    /**
     *  一份生成文本：目标相对路径、片段身份与正文。
     */
    struct GeneratedText
    {
        std::string target_relative = { };   // 相对项目根
        std::string fragment        = { };   // 空表示整文件；否则片段名
        std::string text            = { };   // 产出文本，以 '\n' 结尾
        Digest      digest          = { };   // **整文件生成项**的摘要（text 整文件字节摘要）；
                                             // fragment 非空时必须为零值——片段 N 只能由计划阶段调用
                                             // ID::CMake::fragment_range_digest 计算
    };

    /**
     *  一条生成期行为限制：不是错误、不改变退出码，必须出现在成功摘要与 `--check` 输出里。
     *  装配层只新增这一条输出通道，不新增错误类别（spec §13.2 的类别表不变）。
     */
    struct EmitLimitation
    {
        std::string code    = { };   // 限制代号；首条固定 g8_engine_option_not_projected
        std::string message = { };   // 人读说明：限制了哪条行为、影响面是什么
    };

    /**
     *  一次装配的全部文本。
     */
    struct GeneratedTexts
    {
        // 根 CMakeLists.txt 三段：三段的 digest 都为零值，整文件摘要由计划阶段对组合后的整文件字节算
        GeneratedText root_header        = { };   // 第 1–2 项，只在首次创建时随片段一起组成整文件
        GeneratedText root_output_dirs   = { };   // output-dirs 片段体（第 3–4 项），fragment = "output-dirs"
        GeneratedText root_plugin_targets = { };  // plugin-targets 片段体（第 5–13 项），fragment = "plugin-targets"

        // IDPlugin/CMakeLists.txt
        GeneratedText aggregate = { };

        // 每个插件一份
        std::vector<GeneratedText> plugins = { };   // 按插件名排序

        // src/main.cpp（seed）
        GeneratedText seed = { };

        // 生成期行为限制：由编排层在成功摘要与 --check 输出里逐条打印
        std::vector<EmitLimitation> limitations = { };

        DiagnosticSet diagnostics = { };
        bool          is_valid    = false;
    };

    /**
     *  @brief 由组装结果产出全部文本
     *  @param assembly 已通过 P1 语义校验的组装结果（命名计划必须有效）
     *  @param project_name 项目名（同时是 CMake 工程名与可执行目标名）
     *  @note 相同输入产出逐字节相同的文本：不含时间戳、随机顺序、本机绝对路径
     *  @note 本函数不读写磁盘；`src/main.cpp` 是否已存在由计划阶段判定，这里总是产出内容
     *  @note 不论装配是否成功都产出 `limitations`：准入性缺口（首条固定 `g8_engine_option_not_projected`）
     *        与本次降级项都要写清，避免"生成成功但行为不等价"被读成等价
     *  @note 装配失败（`is_valid` 为假）时不产出任何文本：`ID::CMake::validate` 失败必须发生在任何写入之前，
     *        调用方据 `is_valid` 决定不生成计划
     */
    GeneratedTexts emit_all(const ResolvedAssembly& assembly, std::string_view project_name);

    /**
     *  @brief 把整文件生成文本的摘要回填到资产计划，使 manifest、generation 身份与计划写入项三处一致
     *  @param texts 已装配的文本
     *  @param assets 资产计划；命中项的 `digest` 被覆盖
     *  @note 回填匹配键为 `target_relative`，且只命中 `AssetStage::Generate` 条目；只复制条目不被改写
     *  @note `root_output_dirs` / `root_plugin_targets` 不进入资产计划（受控片段条目由计划阶段按片段身份
     *        单独处理），`root_header` 的整文件摘要由计划阶段对组合后的整文件算，本函数都不回填
     *  @note 只回填 `fragment` 为空的**整文件生成项**；片段条目的 N 由计划阶段用
     *        `ID::CMake::fragment_range_digest` 对写后字节计算，本函数不接收也不产出片段摘要
     */
    void apply_generated_digests(const GeneratedTexts& texts, AssetPlan& assets);
} // namespace ID::Project
