#include "ProductionCaseSupport.hpp"

/**
 *  修订区间非法：min > max 必须拒绝（计划书 7 接口契约）
 *  合法对照：同一候选使用 1..1。
 */

namespace
{
    using namespace ID::Bench::Production;

#if defined(PRODUCTION_CASE_LEGAL)
    inline constexpr auto kCatalog = std::tuple{ entry_leaf(beta()), entry_dep(alpha(), kNameBeta, 1, 1, 1) };
#else
    inline constexpr auto kCatalog = std::tuple{ entry_leaf(beta()), entry_dep(alpha(), kNameBeta, 1, 2, 1) };
#endif

    inline constexpr auto kSelected = std::array<PluginIdentity, 2>{ alpha(), beta() };
    using Shape                     = ShapeOf<Plain<decltype(kCatalog)>, 2, 2, 1, 1>;
    inline constexpr auto kPlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<Shape>(kCatalog, kSelected)));

#if defined(PRODUCTION_CASE_LEGAL)
    static_assert(kPlan.diagnostic.code == PluginError::None, "invalid_revision_range: 合法对照必须组装成功");
#else
    ID_PRODUCTION_EXPECT_ERROR(kPlan, InvalidRevisionRange);
#endif
} // namespace
