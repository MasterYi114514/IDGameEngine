#pragma once

#include <string>

#include "project/Base/Diagnostic.hpp"
#include "project/Resolve/AssetPlan.hpp"
#include "project/Resolve/Closure.hpp"
#include "project/Resolve/Generation.hpp"
#include "project/Resolve/Naming.hpp"

/**
 *  一次解析的完整结果：后续阶段只读本结构，不再回读源清单或用户选择。
 *
 *  构造顺序固定：`expand_direct_selection` → `resolve_closure` → `plan_naming` → `plan_assets`
 *  →（生成单元回填生成项摘要）→ 由计划阶段组装 `generation` 并计算 `generation_id`。
 *
 *  不变量：
 *  1. `assets.files[].digest` 是唯一的内容摘要来源：生成单元写文本后回填，身份计算、`manifest` 与计划
 *     写入项三处都取它，不得各算一次。
 *  2. `generation` / `generation_id` 只由 `GenerationContent` 的五个输入决定，在本单元落地之前保持空值。
 *  3. 本结构是 P1 的输出对象：写入、执行与构建阶段都不得再解析插件清单。
 */

namespace ID::Project
{
    /**
     *  一次 P1 解析的完整结果。
     */
    struct ResolvedAssembly
    {
        Resolution         resolution    = {};
        NamingPlan         naming        = {};
        AssetPlan          assets        = {};      // 生成项 digest 由生成单元回填
        DiagnosticSet      diagnostics   = {};
        bool               is_valid      = false;

        GenerationContent  generation    = {};      // 由计划阶段在摘要回填后组装
        std::string        generation_id = {};      // 由 compute_generation_id 计算
    };
} // namespace ID::Project
