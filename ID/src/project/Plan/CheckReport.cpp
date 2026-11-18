#include "project/Plan/Planner.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/**
 *  `--check` 的三类清单文本（声明在 `Planner.hpp`；本单元只做输出排序与格式化，不重新判定）。
 *
 *  顺序即 spec §13.1 的稳定排序：写入清单 → 删除清单 → 冲突清单，类内按 `(path, fragment)` 字节序。
 *  差异码与冲突码不在这里换算：调用方用 `has_plan_diff` 与 `select_exit_code` 决定退出码。
 */

namespace ID::Project
{
    namespace
    {
        struct ReportLine
        {
            std::string path     = {};
            std::string fragment = {};
            std::string text     = {};
            std::size_t group    = 0;   // 0 写入 / 1 删除 / 2 冲突 / 3 控制面
        };

        bool is_before(const ReportLine& left, const ReportLine& right) noexcept
        {
            if (left.group != right.group)
            {
                return left.group < right.group;
            }
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
                text += '#';
                text += *fragment;
            }
            return text;
        }
    } // namespace

    std::vector<std::string> format_check_report(const ProjectPlan& plan)
    {
        std::vector<ReportLine> lines;

        for (const PlanAction& action : plan.actions)
        {
            ReportLine line;
            line.path     = action.path;
            line.fragment = action.fragment.value_or(std::string());

            switch (action.kind)
            {
                case PlanActionKind::WriteFile:
                case PlanActionKind::WriteFragment:
                    line.group = 0;
                    line.text  = "将写入 " + identity_text(action.path, action.fragment);
                    break;
                case PlanActionKind::DeleteFile:
                    line.group = 1;
                    line.text  = "将删除 " + identity_text(action.path, action.fragment);
                    break;
                case PlanActionKind::RegisterProject:
                    line.group = 3;
                    line.text  = "将在登记文件里新增本项目记录（is_initialized = false）";
                    break;
                case PlanActionKind::WriteSelection:
                    line.group = 3;
                    line.text  = "将写出首次 plugins.json";
                    break;
                case PlanActionKind::UpdateMarker:
                    line.group = 3;
                    line.text  = "将重写 is_initialized 派生缓存";
                    break;
                default:
                    continue;
            }
            lines.push_back(std::move(line));
        }

        for (const PlanConflict& conflict : plan.write_conflicts)
        {
            ReportLine line;
            line.group    = 2;
            line.path     = conflict.path;
            line.fragment = conflict.fragment.value_or(std::string());
            line.text     = "写入冲突 " + std::string(conflict.category) + ' '
                        + identity_text(conflict.path, conflict.fragment) + "：" + conflict.detail;
            lines.push_back(std::move(line));
        }
        for (const PlanConflict& conflict : plan.cleanup_conflicts)
        {
            ReportLine line;
            line.group    = 2;
            line.path     = conflict.path;
            line.fragment = conflict.fragment.value_or(std::string());
            line.text     = "裁剪冲突 " + std::string(conflict.category) + ' '
                        + identity_text(conflict.path, conflict.fragment) + "：" + conflict.detail;
            lines.push_back(std::move(line));
        }

        std::stable_sort(lines.begin(), lines.end(), is_before);

        std::vector<std::string> report;
        report.reserve(lines.size());
        for (const ReportLine& line : lines)
        {
            report.push_back(line.text);
        }
        return report;
    }
} // namespace ID::Project
