#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "project/Input/Linkage.hpp"
#include "project/Input/Selection.hpp"

/**
 *  `--plugin` 参数的三段语法（docs/id/10 §10.4、spec §9.2）。
 *
 *  语法：`<Name>[:static|dynamic][:<OPTION>=<ON|OFF>,...]`。分段固定为"插件名、linkage、选项列表"，
 *  因此插件名不含 `:`、选项值不含 `,`。
 *
 *  不变量：
 *  1. 本单元只判**语法**：段数超出、选项缺 `=`、把第二个插件名拼进同一参数属语法错误；
 *     插件名或选项名查不到由解析阶段给 `plugin_unknown` / `option_unknown`（码 1），本单元不判存在性。
 *  2. `linkage` 未写出时留空：由 CLI 按清单 `requirements` 展开缺省形态后再落盘，不在这里猜。
 *  3. `raw` 保留原始参数文本，供诊断复现同一条命令。
 */

namespace ID::Project
{
    /**
     *  一个 `--plugin` 参数的解析结果。
     */
    struct PluginSpec
    {
        std::string                    name    = {};
        std::optional<Linkage>         linkage = {};    // 未写时为空，落盘前按清单 requirements 补成具体形态
        std::vector<OptionAssignment>  options = {};
        std::string                    raw     = {};    // 原始参数文本，用于诊断复现
    };

    /**
     *  解析结论：`error` 是人读原因，写进 `cli_usage` 的 actual。
     */
    struct PluginSpecParse
    {
        bool        is_ok = false;
        PluginSpec  spec  = {};
        std::string error = {};
    };

    /**
     *  @brief 解析单个 `--plugin` 的取值
     *  @param text 参数取值原文
     *  @return 结论；语法不成立时 `is_ok` 为假并给出人读原因
     *  @note 语法成立但名字查不到由解析阶段给 `plugin_unknown` / `option_unknown`（码 1），本函数不判定
     */
    PluginSpecParse parse_plugin_spec(std::string_view text);
} // namespace ID::Project
