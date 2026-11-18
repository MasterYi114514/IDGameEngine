#pragma once

#include <string>
#include <vector>

#include "cmake/Common/Diagnostic.hpp"
#include "project/Emit/GeneratedTexts.hpp"
#include "project/Resolve/Resolution.hpp"

/**
 *  每个插件的 `CMakeLists.txt` 的模型装配与整文件产出（docs/id/08 §8.5、spec §11.4）。
 *
 *  装配顺序（与骨架项号对应）：第 3 项 `option()` → 第 5 项收源通配 → 目标声明前的 `TARGET` 拒绝守卫 →
 *  第 6 项目标声明 → 第 7 项形态宏与选项宏分支 → 第 8 项编译特性 → 第 9 项输出属性（按类型展开）、
 *  `POSITION_INDEPENDENT_CODE`（仅会被链进 `SHARED` 的静态目标）与 `BUILD_RPATH`（仅 `SHARED`）→
 *  第 10 项 include → 第 12 项链接（`Common` 为 `PUBLIC`、上游无条件边与条件边、vendor 目标）。
 *
 *  取值口径（唯一来源，不回读清单）：
 *  1. `source_dirs`、`include_roots`（含 `is_system`）、`vendor_links` 只取 `Resolution.compile_facts`；
 *     `is_system` 一律直接取模型取值，**不按路径前缀推断**。
 *  2. 形态只由 `selection.all` 的 `linkage` 决定，生成文件里不出现形态 `option`；静态写一条
 *     `PUBLIC <静态宏>`，动态写一条 `PRIVATE <导出宏>`。
 *  3. `option()` 的说明串取 `PluginNaming::option_descriptions` 与 `option_vars` **同下标**的那一位
 *     （清单 `options[].description` 原文）：转义交给 CMake 字符串渲染，装配层不重复转义；
 *     该位为空串时写占位串并登记一条 `option_description_missing`，不静默留空。
 *  4. 选项效果只由 `defines` 与 `links_when_on` 表达：带 `defines` 的选项写成互斥 `if()/else()`，
 *     `ON` 定义 `<宏>=1`、`OFF` 定义 `<宏>=0`；受控边合并成单个 `if(A OR B)`，且不出现在无条件块里。
 *  5. 条件边与无条件边由 `Resolution.edges` 决定；`vendor_links` 中 `target` 非空的条目以 `PRIVATE`
 *     链接，本文件**不**写 `add_subdirectory`、不写 vendor 接入与守卫。
 *
 *  `option_vars` / `option_defines` / `option_descriptions` 三者同长度、同序（按选项名 UTF-8 字节序），
 *  同一下标对应同一选项；`option_defines` 该位为空串表示该选项不产生宏。装配层按**下标**配对，
 *  不按宏名排序、也不跳过无 `defines` 的选项，并在产出文本之前断言"选项宏分支按选项名序输出"。
 *  手工装配 `PluginNaming`（探针与测试用例）必须同时填三个列表：缺一即违反模型口径，装配层以断言终止。
 */

namespace ID::Project
{
    /**
     *  一个插件文件的装配结果：插件名（同时是目标名与目录名）、整文件文本、校验报告与装配期限制。
     */
    struct PluginFileResult
    {
        std::string                 plugin      = { };   // 插件名
        GeneratedText               text        = { };   // IDPlugin/<Plugin>/CMakeLists.txt；校验失败时为空
        CMake::ValidationReport     report      = { };   // 该插件模型的校验报告
        std::vector<EmitLimitation> limitations = { };   // 该插件产出的限制（选项名序），非错误、不改退出码
    };

    /**
     *  全部插件文件的装配结果。
     */
    struct PluginEmitResult
    {
        std::vector<PluginFileResult> files = { };   // 按插件名排序
    };

    /**
     *  @brief 为闭包内每个插件装配模型并产出整文件文本
     *  @param assembly 组装结果（只读；取命名计划、编译面事实、边集合与有效选择）
     *  @return 逐插件的文本、校验报告与装配期限制，按插件名排序
     *  @note 不写盘、不读磁盘；相同输入产出逐字节相同的文本
     *  @note 任一插件模型校验失败时只记录该插件的报告，不产出它的文本
     */
    PluginEmitResult emit_plugins(const ResolvedAssembly& assembly);
} // namespace ID::Project
