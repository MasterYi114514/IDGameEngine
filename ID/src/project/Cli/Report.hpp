#pragma once

#include <iostream>
#include <ostream>
#include <span>
#include <string_view>

#include "project/Apply/Actor.hpp"
#include "project/Base/Diagnostic.hpp"
#include "project/Build/Builder.hpp"
#include "project/Plan/PlanTypes.hpp"
#include "project/Record/ResolvedRecord.hpp"

/**
 *  `ID project` 的人读输出（docs/id/10 §10.6、`skills/id-kernel/references/foundation.md` §7）。
 *
 *  本单元只负责把已有结论排版：不判成败、不改退出码、不筛选诊断。
 *
 *  不变量：
 *  1. 一条诊断六行：五要素「类别 / 对象 / 期望 / 实际 / 下一步」各一行，定位单列一行；缩进两级。
 *  2. 诊断按 `DiagnosticSet::sorted()` 的顺序输出，警告与错误都到 `err`；成功信息只到 `out`。
 *  3. `--check` 的三类清单顺序固定：写入 → 删除 → 冲突，类内按 `(path, fragment)` 字节序；差异不写成"失败"。
 *  4. `limitations` 是记录的公开行为限制，不是错误也不是警告：输出到 `out`，措辞不含"失败"，不改退出码。
 *  5. 成功摘要不打印日志路径：非构建命令不产生日志文件，构建日志路径只在 `build.json` 的 `log_path` 里表达。
 */

namespace ID::Project
{
    /**
     *  命令输出通道：缺省是标准输出与标准错误。
     */
    struct CommandIo
    {
        std::ostream* out = &std::cout;
        std::ostream* err = &std::cerr;
    };

    /**
     *  输出选项；`is_check` 影响 `--check` 的措辞。
     */
    struct ReportOptions
    {
        bool is_check   = false;
        bool is_verbose = false;
    };

    /**
     *  @brief 输出单条诊断
     *  @param stream 目标流
     *  @param diagnostic 已填好五要素的诊断
     *  @note 共六行，缩进两级；`类别` 行给出可被脚本断言的稳定标识
     */
    void print_diagnostic(std::ostream& stream, const ProjectDiagnostic& diagnostic);

    /**
     *  @brief 按稳定顺序输出全部诊断
     *  @param stream 目标流
     *  @param diagnostics 诊断集合
     */
    void print_diagnostics(std::ostream& stream, const DiagnosticSet& diagnostics);

    /**
     *  @brief `--check` 的三类清单
     *  @param stream 目标流
     *  @param plan 计划
     *  @note 顺序：写入清单 → 删除清单 → 冲突清单 → 汇总行（含退出码含义）→ 登记写入行 → limitations
     */
    void print_check_report(std::ostream& stream, const ProjectPlan& plan);

    /**
     *  @brief 输出一行逐文件动作结果
     *  @param stream 目标流
     *  @param verb `written` / `unchanged` / `removed` / `backup`
     *  @param relative 相对项目根的路径
     */
    void print_action_result(std::ostream& stream, std::string_view verb, std::string_view relative);

    /**
     *  @brief 输出 limitations：每条一行 `code: message`
     *  @param stream 目标流
     *  @param limitations 取自 `resolved` 记录
     *  @note 为空时不输出；它不是错误、不是警告，不改变退出码
     */
    void print_limitations(std::ostream& stream, std::span<const ResolvedLimitation> limitations);

    /**
     *  @brief 成功收尾摘要
     *  @param stream 目标流
     *  @param run 执行结果
     *  @param build 构建结论；非构建命令为空
     *  @note 输出产物路径与记录路径各一行；不打印日志路径（日志路径只在 `build.json` 的 `log_path` 里）
     */
    void print_success_summary(std::ostream& stream, const RunResult& run, const BuildOutcome* build);
} // namespace ID::Project
