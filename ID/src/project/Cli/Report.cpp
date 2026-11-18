#include "project/Cli/Report.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "project/Base/Error.hpp"
#include "project/Base/Text.hpp"
#include "project/Record/RecordPaths.hpp"

/**
 *  `ID project` 的人读输出实现（docs/id/10 §10.6）。
 *
 *  排版只做三件事：把诊断的六行写全、把 `--check` 的三类清单按固定顺序列出并给出汇总、把成功收尾的产物与
 *  记录路径各写一行。差异不写成"失败"，`limitations` 不写成警告。
 */

namespace ID::Project
{
    namespace
    {
        // 诊断与清单项的缩进：两级
        constexpr std::string_view kIndent = "    ";

        struct ReportLine
        {
            std::string  path     = {};
            std::string  fragment = {};
            std::string  text     = {};
            std::size_t  group    = 0;   // 0 写入 / 1 删除 / 2 冲突 / 3 控制面
        };

        bool is_before(const ReportLine& left, const ReportLine& right) noexcept
        {
            if (left.path != right.path)
            {
                return left.path < right.path;
            }
            return left.fragment < right.fragment;
        }

        std::string identity_text(std::string_view path, const std::optional<std::string>& fragment)
        {
            std::string text(path);
            if (fragment.has_value())
            {
                text.push_back('#');
                text.append(*fragment);
            }
            return text;
        }
    } // namespace

    void print_diagnostic(std::ostream& stream, const ProjectDiagnostic& diagnostic)
    {
        stream << kIndent << "类别: " << to_identifier(diagnostic.category) << "（"
               << to_text(diagnostic.category) << "）\n";
        stream << kIndent << "对象: " << diagnostic.subject << '\n';
        stream << kIndent << "期望: " << diagnostic.expected << '\n';
        stream << kIndent << "实际: " << diagnostic.actual << '\n';
        stream << kIndent << "定位: " << (diagnostic.location.empty() ? std::string("（无）") : diagnostic.location)
               << '\n';
        stream << kIndent << "下一步: " << diagnostic.next_action << '\n';
    }

    void print_diagnostics(std::ostream& stream, const DiagnosticSet& diagnostics)
    {
        if (diagnostics.is_empty())
        {
            return;
        }

        const DiagnosticSet sorted = diagnostics.sorted();
        for (const ProjectDiagnostic& diagnostic : sorted.span())
        {
            print_diagnostic(stream, diagnostic);
        }
    }

    void print_check_report(std::ostream& stream, const ProjectPlan& plan)
    {
        std::vector<ReportLine> writes;
        std::vector<ReportLine> deletes;
        std::vector<ReportLine> conflicts;
        std::vector<ReportLine> controls;

        for (const PlanAction& action : plan.actions)
        {
            ReportLine line;
            line.path     = action.path;
            line.fragment = action.fragment.value_or(std::string());

            switch (action.kind)
            {
                case PlanActionKind::WriteFile:
                case PlanActionKind::WriteFragment:
                    line.text = "将写入 " + identity_text(action.path, action.fragment);
                    writes.push_back(std::move(line));
                    break;
                case PlanActionKind::DeleteFile:
                    line.text = "将删除 " + identity_text(action.path, action.fragment);
                    deletes.push_back(std::move(line));
                    break;
                case PlanActionKind::RegisterProject:
                    line.text = "将在登记文件里新增本项目记录（is_initialized = false）";
                    controls.push_back(std::move(line));
                    break;
                case PlanActionKind::WriteSelection:
                    line.text = "将写出首次 plugins.json";
                    controls.push_back(std::move(line));
                    break;
                case PlanActionKind::UpdateMarker:
                    line.text = "将重写 is_initialized 派生缓存";
                    controls.push_back(std::move(line));
                    break;
                default:
                    break;
            }
        }

        for (const PlanConflict& conflict : plan.write_conflicts)
        {
            ReportLine line;
            line.path     = conflict.path;
            line.fragment = conflict.fragment.value_or(std::string());
            line.text     = "写入冲突 " + conflict.category + ' ' + identity_text(conflict.path, conflict.fragment)
                        + "：" + conflict.detail;
            conflicts.push_back(std::move(line));
        }
        for (const PlanConflict& conflict : plan.cleanup_conflicts)
        {
            ReportLine line;
            line.path     = conflict.path;
            line.fragment = conflict.fragment.value_or(std::string());
            line.text     = "裁剪冲突 " + conflict.category + ' ' + identity_text(conflict.path, conflict.fragment)
                        + "：" + conflict.detail;
            conflicts.push_back(std::move(line));
        }

        std::stable_sort(writes.begin(), writes.end(), is_before);
        std::stable_sort(deletes.begin(), deletes.end(), is_before);
        std::stable_sort(conflicts.begin(), conflicts.end(), is_before);

        stream << "写入清单（" << writes.size() << " 项）:\n";
        for (const ReportLine& line : writes)
        {
            stream << kIndent << line.text << '\n';
        }

        stream << "删除清单（" << deletes.size() << " 项）:\n";
        for (const ReportLine& line : deletes)
        {
            stream << kIndent << line.text << '\n';
        }

        stream << "冲突清单（" << conflicts.size() << " 项）:\n";
        for (const ReportLine& line : conflicts)
        {
            stream << kIndent << line.text << '\n';
        }

        stream << "汇总: 写入 " << writes.size() << " / 删除 " << deletes.size() << " / 冲突 " << conflicts.size()
               << '\n';

        // 差异不写成"失败"：只说明这一状态对应的退出码含义
        if (!conflicts.empty())
        {
            stream << "退出码: 存在冲突，按冲突类别取码（写入冲突 2 / 裁剪冲突 5）\n";
        }
        else if (!writes.empty() || !deletes.empty() || !controls.empty())
        {
            stream << "退出码: 存在计划变更，退出码 6（不是失败，也不是已一致）\n";
        }
        else
        {
            stream << "退出码: 无计划变更，退出码 0\n";
        }

        for (const ReportLine& line : controls)
        {
            stream << kIndent << line.text << '\n';
        }

        print_limitations(stream, plan.next_resolved.limitations);
    }

    void print_action_result(std::ostream& stream, std::string_view verb, std::string_view relative)
    {
        stream << verb << ' ' << relative << '\n';
    }

    void print_limitations(std::ostream& stream, std::span<const ResolvedLimitation> limitations)
    {
        for (const ResolvedLimitation& limitation : limitations)
        {
            stream << kIndent << limitation.code << ": " << limitation.message << '\n';
        }
    }

    void print_success_summary(std::ostream& stream, const RunResult& run, const BuildOutcome* build)
    {
        if (build != nullptr)
        {
            for (const BuildArtifact& artifact : build->record.artifacts)
            {
                stream << "产物: " << artifact.path << '\n';
            }
            stream << "构建结论: " << build->record.verdict << '\n';
        }

        stream << "记录: " << kResolvedRelative << '\n';
        stream << "记录: " << kManifestRelative << '\n';
        stream << "组装: " << (run.is_committed ? "已提交" : "本次未提交内容") << '\n';
    }
} // namespace ID::Project
