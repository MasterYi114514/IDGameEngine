#include "ProductionCaseSupport.hpp"

/**
 *  唯一候选的接口修订不达标：RevisionMismatch（计划书 6.2）
 *  合法对照：同一提供者修订 1，契约 1..1。
 */

namespace
{
    using namespace ID::Bench::Production;

#if defined(PRODUCTION_CASE_LEGAL)
    inline constexpr auto kCatalog = std::tuple{ entry_leaf(beta()), entry_dep(alpha(), kNameBeta, 1, 1, 1) };
#else
    inline constexpr auto kCatalog = std::tuple{ entry_leaf(beta()), entry_dep(alpha(), kNameBeta, 1, 2, 2) };
#endif

    inline constexpr auto kSelected = std::array<PluginIdentity, 2>{ alpha(), beta() };
    using Shape                     = ShapeOf<Plain<decltype(kCatalog)>, 2, 2, 1, 1>;
    inline constexpr auto kPlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<Shape>(kCatalog, kSelected)));

#if defined(PRODUCTION_CASE_LEGAL)
    static_assert(kPlan.diagnostic.code == PluginError::None, "revision_mismatch: 合法对照必须组装成功");
#else
    ID_PRODUCTION_EXPECT_ERROR(kPlan, RevisionMismatch);
#endif
} // namespace
