#include "ProductionCaseSupport.hpp"

/**
 *  选中后的固定图存在真实环：Cycle（计划书 6.3 / 7）
 *  合法对照：去掉一条边后无环。
 */

namespace
{
    using namespace ID::Bench::Production;

#if defined(PRODUCTION_CASE_LEGAL)
    inline constexpr auto kCatalog = std::tuple{ entry_dep(alpha(), kNameBeta), entry_leaf(beta()) };
#else
    inline constexpr auto kCatalog = std::tuple{ entry_dep(alpha(), kNameBeta), entry_dep(beta(), kNameAlpha) };
#endif

    inline constexpr auto kSelected = std::array<PluginIdentity, 2>{ alpha(), beta() };
    // 反向分支在报 Cycle 前已经产生 2 条绑定，证据容量必须覆盖，否则会先在证据数组越界处失败
    using Shape                     = ShapeOf<Plain<decltype(kCatalog)>, 2, 2, 2, 2>;
    inline constexpr auto kPlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<Shape>(kCatalog, kSelected)));

#if defined(PRODUCTION_CASE_LEGAL)
    static_assert(kPlan.diagnostic.code == PluginError::None, "cycle: 合法对照必须组装成功");
    static_assert(kPlan.topo_order[0] == kPlan.find_active("ID.Case.Beta"), "cycle: 合法对照 Beta 必须先排");
#else
    ID_PRODUCTION_EXPECT_ERROR(kPlan, Cycle);
#endif
} // namespace
