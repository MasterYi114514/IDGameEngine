#pragma once

#include <string>
#include <vector>

#include "project/Cli/PluginSpec.hpp"
#include "project/Input/PluginCatalog.hpp"

/**
 *  交互式插件选择（docs/id/10 §10.5、spec §9.2）。
 *
 *  界面形态不是契约；契约是两件事：**非交互环境不读 stdin、不等待输入**，并且交互结果最终被转换成与
 *  `--plugin` 相同的 `PluginSpec` 序列——只有一套选择构建逻辑。
 *
 *  不变量：
 *  1. `stdin` 不是终端时直接返回 `is_ok = false` 与原因，不读、不阻塞。
 *  2. 选中下游插件时自动勾选它的上游依赖（按清单 `dependencies`），结果按插件名排序。
 *  3. "不选任何插件"必须由用户显式确认（输入 `0`），否则继续询问；空选择只在显式确认时成立。
 */

namespace ID::Project
{
    /**
     *  一次交互的结果：完成、取消或环境不允许交互。
     */
    struct InteractiveResult
    {
        bool                     is_ok        = false;   // 用户完成选择或环境不允许交互
        bool                     is_cancelled = false;
        std::vector<PluginSpec>  plugins       = {};
        bool                     is_empty      = false;   // 用户显式确认"不选任何插件"
        std::string              reason        = {};      // 不能交互的原因（stdin 非终端等）
    };

    /**
     *  @brief 判断标准输入是否为终端
     *  @return 是终端返回 true；重定向或管道返回 false
     */
    bool is_stdin_terminal() noexcept;

    /**
     *  @brief 交互式选择：列出可发现插件、下游选中自动勾选上游、逐插件设置 linkage 与选项
     *  @param catalog 已发现的插件
     *  @return 结果；非交互环境直接返回 `is_ok = false` 与原因，不读 stdin、不等待
     *  @note 必须能用 `--plugin` 完整复现同等结果（spec §9.2），因此每个选择都带显式 linkage 与全部选项取值
     */
    InteractiveResult run_interactive_selection(const PluginCatalog& catalog);
} // namespace ID::Project
