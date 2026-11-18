#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "project/Base/Diagnostic.hpp"
#include "project/Cli/PluginSpec.hpp"

/**
 *  `ID project <子命令>` 的参数词法（docs/id/10 §10.3、spec §9.1）。
 *
 *  本单元只做词法：未知子命令、未知参数、缺取值、重复且取值不同、互斥组合、缺必填参数一律
 *  `cli_usage`（3）。谁该被选中、路径是否合法、记录是否可信都不在这里判定。
 *
 *  不变量：
 *  1. `--check` 与 `--dry-run` 是同一语义的两个别名，五个子命令都接受。
 *  2. 同一参数重复给出且取值不同 → `cli_usage`；取值相同视为同一请求，不产生第二条诊断。
 *  3. `--plugin` 可重复，但同一插件名出现两次 → `cli_usage`（不合并、不去重、不后者覆盖前者）。
 *  4. `list` 不收任何参数，包括 `--check`。
 *  5. 诊断的 `next_action` 给出可直接复制的完整调用形式，脚本与人都能照着重跑。
 */

namespace ID::Project
{
    /**
     *  子命令。取值与 spec §9.1 的命令表顺序一致。
     */
    enum class Subcommand : std::uint8_t
    {
        New     = 0,
        Update  = 1,
        Build   = 2,
        Clean   = 3,
        Recover = 4,
        List    = 5,
    };

    /**
     *  一次解析出的请求：全部字段都是命令行原文或已解析的 `PluginSpec`。
     */
    struct CommandRequest
    {
        Subcommand               command   = Subcommand::Update;
        std::string              name      = {};    // 项目名（`new` 取 --name，其它命令取位置参数）；list 为空
        std::string              path      = {};    // `new --path` 原文
        std::vector<PluginSpec>  plugins   = {};    // `--plugin` 解析结果，保留命令行顺序（查重用）
        bool                     is_empty  = false; // --empty
        bool                     is_check  = false; // --check / --dry-run
        bool                     is_build  = false; // --build
        bool                     is_force  = false; // --force
        std::string              config    = {};    // --config
        std::string              generator = {};    // --generator
        std::string              toolchain = {};    // --toolchain
        std::string              arch      = {};    // --arch
        std::string              cmake     = {};    // --cmake
        std::string              action    = {};    // recover --action
    };

    /**
     *  解析结论：`is_ok` 为假时 `diagnostics` 至少有一条 `cli_usage`。
     */
    struct CommandParse
    {
        bool            is_ok       = false;
        CommandRequest  request     = {};
        DiagnosticSet   diagnostics = {};
    };

    /**
     *  @brief 解析 `project` 之后的参数（不含 "project" 本身）
     *  @param arguments 参数数组
     *  @return 解析结论
     *  @note 规则（spec §9.1、§9.2）：未知子命令 / 未知参数 / 缺取值 / 缺子命令 → `cli_usage`；
     *        `--check` 与 `--dry-run` 等价且与 `--build` 互斥；同一参数重复且取值不同 → `cli_usage`；
     *        `--plugin` 与 `--empty` 互斥；同一插件名重复 → `cli_usage`；`list` 不收任何参数
     *  @note 必填参数缺失（`new` 的 `--name`/`--path`、`recover` 的 `--action`、`build` 的 `--config`、
     *        除 `new`/`list` 外的项目名）同样按 `cli_usage`，诊断给出可复制的完整调用形式
     */
    CommandParse parse_command_line(std::span<const std::string> arguments);

    /**
     *  @brief 顶层用法文本
     *  @return 与 `cmake self` 的用法并列的 `project` 用法文本（静态存储）
     *  @note 两套用法文本分开打印、互不覆盖（spec §14.1 G5）
     */
    std::string_view project_usage() noexcept;
} // namespace ID::Project
