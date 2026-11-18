#include "project/Base/Diagnostic.hpp"

#include <algorithm>
#include <string_view>
#include <utility>

/**
 *  DiagnosticSet 的稳定排序与 select_exit_code 的全序取码实现。
 *
 *  分组口径：先按 exit_priority 分组（0 最优先），warn 类别排在所有非 warn 组之后；
 *  组内按 subject 的 UTF-8 字节序升序。排序用 stable_sort，因此 subject 完全相同时保持发现顺序，
 *  同一输入重复运行得到同一输出。
 */

namespace ID::Project
{
    namespace
    {
        // warn 组的分组序号：排在 0–5 之后，只用于排序，不参与退出码选择
        inline constexpr std::uint8_t kWarnGroup = 6;

        // 越界调用 at() 时返回的空载体：静态存储，不参与业务语义
        const ProjectDiagnostic& empty_diagnostic() noexcept
        {
            static const ProjectDiagnostic kEmpty{ProjectCategory::CliUsage, {}, {}, {}, {}, {}};
            return kEmpty;
        }

        std::uint8_t sort_group(ProjectCategory category) noexcept
        {
            const std::uint8_t priority = exit_priority(category);
            return priority == kNoPriority ? kWarnGroup : priority;
        }
    } // namespace

    bool is_warning(ProjectCategory category) noexcept
    {
        return !exit_mapping(category).is_mapped;
    }

    void DiagnosticSet::add(ProjectDiagnostic diagnostic)
    {
        m_diagnostics.push_back(std::move(diagnostic));
    }

    bool DiagnosticSet::is_empty() const noexcept
    {
        return m_diagnostics.empty();
    }

    bool DiagnosticSet::has_error() const noexcept
    {
        for (const ProjectDiagnostic& diagnostic : m_diagnostics)
        {
            if (!is_warning(diagnostic.category))
            {
                return true;
            }
        }
        return false;
    }

    bool DiagnosticSet::has_category(ProjectCategory category) const noexcept
    {
        for (const ProjectDiagnostic& diagnostic : m_diagnostics)
        {
            if (diagnostic.category == category)
            {
                return true;
            }
        }
        return false;
    }

    std::size_t DiagnosticSet::size() const noexcept
    {
        return m_diagnostics.size();
    }

    const ProjectDiagnostic& DiagnosticSet::at(std::size_t index) const noexcept
    {
        if (index >= m_diagnostics.size())
        {
            return empty_diagnostic();
        }
        return m_diagnostics[index];
    }

    DiagnosticSet DiagnosticSet::sorted() const
    {
        DiagnosticSet result;
        result.m_diagnostics = m_diagnostics;

        std::stable_sort(result.m_diagnostics.begin(), result.m_diagnostics.end(),
                         [](const ProjectDiagnostic& left, const ProjectDiagnostic& right) noexcept
                         {
                             const std::uint8_t left_group  = sort_group(left.category);
                             const std::uint8_t right_group = sort_group(right.category);
                             if (left_group != right_group)
                             {
                                 return left_group < right_group;
                             }

                             // UTF-8 字节序等于码点序：不解码即可满足"类别内对象名码点升序"
                             return std::string_view(left.subject) < std::string_view(right.subject);
                         });
        return result;
    }

    std::span<const ProjectDiagnostic> DiagnosticSet::span() const noexcept
    {
        return std::span<const ProjectDiagnostic>(m_diagnostics.data(), m_diagnostics.size());
    }

    int select_exit_code(std::span<const ProjectDiagnostic> diagnostics, bool has_plan_diff) noexcept
    {
        std::uint8_t best_priority = kNoPriority;
        int          best_code     = kExitSuccess;

        for (const ProjectDiagnostic& diagnostic : diagnostics)
        {
            const std::uint8_t priority = exit_priority(diagnostic.category);
            if (priority == kNoPriority)
            {
                // warn 类别不参与选择：既不改变退出码，也不占用优先级
                continue;
            }

            if (priority < best_priority)
            {
                best_priority = priority;
                best_code     = exit_mapping(diagnostic.category).code;
            }
        }

        if (best_priority != kNoPriority)
        {
            return best_code;
        }

        // 没有任何非 warn 诊断：只有变更、没有冲突时给差异码，否则成功
        return has_plan_diff ? kExitCheckDiff : kExitSuccess;
    }
} // namespace ID::Project
