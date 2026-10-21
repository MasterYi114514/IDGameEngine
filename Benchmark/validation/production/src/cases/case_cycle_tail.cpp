#include "ProductionCaseSupport.hpp"

/**
 *  环诊断只报告真实闭合环，不把全部残余入度节点都算成环成员（计划书 7 / 16.1）
 *  图：Beta -> Alpha，Gamma -> Beta，Alpha -> Gamma（三元环），Alpha -> Delta（尾部下游）。
 *  合法对照：去掉 Alpha -> Gamma 这条边，其余不变。
 */

namespace
{
    using namespace ID::Bench::Production;

#if defined(PRODUCTION_CASE_LEGAL)
    inline constexpr auto kCatalog = std::tuple{
        entry_dep(alpha(), kNameBeta), entry_dep(beta(), kNameGamma), entry_leaf(gamma()), entry_dep(delta(), kNameAlpha) };
#else
    inline constexpr auto kCatalog = std::tuple{
        entry_dep(alpha(), kNameBeta), entry_dep(beta(), kNameGamma), entry_dep(gamma(), kNameAlpha),
        entry_dep(delta(), kNameAlpha) };
#endif

    inline constexpr auto kSelected = std::array<PluginIdentity, 4>{ alpha(), beta(), gamma(), delta() };
    using Shape                     = ShapeOf<Plain<decltype(kCatalog)>, 4, 4, 4, 4>;
    inline constexpr auto kPlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<Shape>(kCatalog, kSelected)));

#if defined(PRODUCTION_CASE_LEGAL)
    static_assert(kPlan.diagnostic.code == PluginError::None, "cycle_tail: 合法对照必须组装成功");
    static_assert(kPlan.edge_count == 3, "cycle_tail: 合法对照有三条边");
#else
    static_assert(cycle_path_is_closed(kPlan), "cycle_tail: error_nodes 必须是真实闭合环路径");
    static_assert(kPlan.error_node_count == 3, "cycle_tail: 真实环成员必须是三个，不能包含尾部下游");
    ID_PRODUCTION_EXPECT_ERROR(kPlan, Cycle);
#endif
} // namespace
