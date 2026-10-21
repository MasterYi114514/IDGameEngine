#pragma once

#include <array>
#include <string_view>
#include <tuple>

#include "FixturePluginApi.hpp"

/**
 *  反向编译用例的公共地基
 *  1. 每个 case 的"合法对照"与"期望失败"两个分支共用同一个 TU、同一组 include 与编译参数，
 *     只有输入数据不同；因此合法分支编译通过就证明"环境是好的"，失败不是环境坏了；
 *  2. 非法分支必须真实断言 `kPlan.error == 期望类别`：辅助模板 error_matches / error_differs
 *     是两个互斥的常量谓词，配成一对 static_assert 后，
 *       - 实际类别 == 期望类别：只有 error_differs 这一条失败，诊断带 ID_ASSEMBLY_ERROR_EXPECTED:<类别>；
 *       - 实际类别 != 期望类别：只有 error_matches 这一条失败，诊断带 ID_ASSEMBLY_ERROR_MISMATCH:<类别>。
 *     驱动器要求 EXPECTED 出现且 MISMATCH 不出现，因此"任意错误都算通过"的假阳性不可能发生；
 *  3. 同一形态也用于驱动器的反向自检（故意写错期望类别的 TU 必须被拒绝）。
 */

namespace ID::Bench::Fixture::Cases
{
    inline constexpr char kNameAlpha[] = "ID.Case.Alpha";
    inline constexpr char kNameBeta[]  = "ID.Case.Beta";
    inline constexpr char kNameGamma[] = "ID.Case.Gamma";
    inline constexpr char kNameGhost[] = "ID.Case.Ghost";       // 只出现在候选里，不在 catalog
    inline constexpr char kNameBad[]   = "ID.Case.Alpha!";      // 含非法字符 '!'

    consteval PluginIdentity alpha() noexcept
    {
        return register_plugin(kNameAlpha);
    }

    consteval PluginIdentity beta() noexcept
    {
        return register_plugin(kNameBeta);
    }

    consteval PluginIdentity gamma() noexcept
    {
        return register_plugin(kNameGamma);
    }

    // 单候选需求组
    consteval std::array<std::string_view, 1> group_of(std::string_view name) noexcept
    {
        return { name };
    }

    // 两候选需求组（OR）
    consteval std::array<std::string_view, 2> pair_of(std::string_view left, std::string_view right) noexcept
    {
        return { left, right };
    }

    /**
     *  @brief 断言计划的错误类别**等于**期望类别
     *  @tparam Expected 期望的错误类别
     *  @param plan sort_and_validate 的结果
     *  @return 实际类别与期望相同时为真
     *  @note 与 error_differs 配对使用；单独使用不构成反向用例（编译不会失败）
     */
    template <ErrorCode Expected, typename Plan>
    consteval bool error_matches(const Plan& plan) noexcept
    {
        return plan.error == Expected;
    }

    /**
     *  @brief 断言计划的错误类别**不等于**期望类别
     *  @tparam Expected 期望的错误类别
     *  @param plan sort_and_validate 的结果
     *  @return 实际类别与期望不同时为真
     *  @note 反向用例里它必须失败（即条件为假），失败消息携带 EXPECTED 标记；
     *        实际类别写错时它反而成立，于是只剩 error_matches 的 MISMATCH 消息
     */
    template <ErrorCode Expected, typename Plan>
    consteval bool error_differs(const Plan& plan) noexcept
    {
        return plan.error != Expected;
    }

    /**
     *  @brief 校验 error_nodes 记录的是**真实闭合环路径**（不是残余入度节点集合）
     *  @param plan sort_and_validate 的结果
     *  @return 环标记成立、成员数在 [2, active_count]、成员互不重复、
     *          且 error_nodes[i+1] 是 error_nodes[i] 的一个上游（末尾回到 [0]）
     *  @note 方向口径与 AssemblyResult::error_nodes 的注释一致：沿"下游 -> 上游"闭合。
     *        只做只读检查，不改变计划。
     */
    template <typename Plan>
    consteval bool cycle_path_is_closed(const Plan& plan) noexcept
    {
        if (!plan.error_is_cycle || plan.error_node_count < 2 || plan.error_node_count > plan.active_count)
        {
            return false;
        }
        for (uint16_t i = 0; i < plan.error_node_count; ++i)
        {
            const uint16_t dependent = plan.error_nodes[i];
            const uint16_t upstream  = plan.error_nodes[(i + 1) % plan.error_node_count];
            if (dependent >= plan.active_count || upstream >= plan.active_count)
            {
                return false;
            }
            bool has_edge = false;
            for (uint16_t e = plan.edge_begin[dependent]; e < plan.edge_begin[dependent + 1]; ++e)
            {
                if (plan.edge_target[e] == upstream)
                {
                    has_edge = true;
                    break;
                }
            }
            if (!has_edge)
            {
                return false;
            }
            for (uint16_t j = i + 1; j < plan.error_node_count; ++j)
            {
                if (plan.error_nodes[j] == dependent)
                {
                    return false;       // 成员重复：不是环路径
                }
            }
        }
        return true;
    }
} // namespace ID::Bench::Fixture::Cases

/**
 *  @brief 反向用例的类别契约：实际类别必须严格等于 code
 *  @param plan 计划表达式（可含逗号，因此用可变参数）
 *  @param code 期望的错误类别（ErrorCode 枚举名）
 *  @note 两条 static_assert 互斥：编译失败一定发生，且消息能区分"类别正确"与"类别不符"
 */
#define ID_FIXTURE_EXPECT_ERROR(plan, code)                                                                          \
    static_assert(::ID::Bench::Fixture::Cases::error_matches<::ID::Bench::Fixture::ErrorCode::code>(plan),            \
                  "ID_ASSEMBLY_ERROR_MISMATCH:" #code);                                                              \
    static_assert(::ID::Bench::Fixture::Cases::error_differs<::ID::Bench::Fixture::ErrorCode::code>(plan),            \
                  "ID_ASSEMBLY_ERROR_EXPECTED:" #code)
