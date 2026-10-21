#include "CaseCommon.hpp"

/**
 *  选中图环：绑定完成后固定图存在环必须拒绝（计划书 6.3）
 *  合法对照：同样的两个插件，只有 B -> A 一条边。
 */

namespace
{
    using namespace ID::Bench::Fixture;
    using namespace ID::Bench::Fixture::Cases;

#if defined(FIXTURE_CASE_LEGAL)
    consteval auto make_case_plan() noexcept
    {
        constexpr auto declaration_alpha = declare_dependencies(alpha(), std::tuple<>{});
        constexpr auto declaration_beta  = declare_dependencies(beta(), std::tuple{ group_of(kNameAlpha) });
        constexpr std::array<PluginIdentity, 2> selected{ alpha(), beta() };
        return sort_and_validate(std::tuple{ declaration_alpha, declaration_beta }, selected);
    }
#else
    consteval auto make_case_plan() noexcept
    {
        constexpr auto declaration_alpha = declare_dependencies(alpha(), std::tuple{ group_of(kNameBeta) });
        constexpr auto declaration_beta  = declare_dependencies(beta(), std::tuple{ group_of(kNameAlpha) });
        constexpr std::array<PluginIdentity, 2> selected{ alpha(), beta() };
        return sort_and_validate(std::tuple{ declaration_alpha, declaration_beta }, selected);
    }
#endif

    constexpr auto kPlan = make_case_plan();

#if defined(FIXTURE_CASE_LEGAL)
    static_assert(kPlan.error == ErrorCode::None, "cycle: 合法对照必须组装成功");
    static_assert(kPlan.topo_order[0] == find_active_index(kPlan, kNameAlpha), "合法对照的拓扑序必须是 A -> B");
#else
    ID_FIXTURE_EXPECT_ERROR(kPlan, Cycle);
    // 类别正确还不够：必须证明记录的是真实环路径（两个成员、且按真实边首尾相接）
    static_assert(kPlan.error_is_cycle, "cycle: error_nodes 必须是真实环路径而不是残余入度节点");
    static_assert(kPlan.error_node_count == 2, "cycle: 两节点环必须记录恰好两个成员");
    static_assert(kPlan.error_nodes[0] == find_active_index(kPlan, kNameAlpha) &&
                      kPlan.error_nodes[1] == find_active_index(kPlan, kNameBeta),
                  "cycle: 环成员必须是 Alpha 与 Beta");
    static_assert(cycle_path_is_closed(kPlan), "cycle: 记录的节点必须按真实依赖边闭合");
#endif
} // namespace
