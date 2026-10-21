#include "ProductionCaseSupport.hpp"

/**
 *  重复 active：同一插件在选中集合出现两次必须拒绝（计划书 7 产品选择）
 *  合法对照：两个不同插件各出现一次。
 */

namespace
{
    using namespace ID::Bench::Production;

    inline constexpr auto kCatalog = std::tuple{ entry_leaf(alpha()), entry_leaf(beta()) };

#if defined(PRODUCTION_CASE_LEGAL)
    inline constexpr auto kSelected = std::array<PluginIdentity, 2>{ alpha(), beta() };
#else
    inline constexpr auto kSelected = std::array<PluginIdentity, 2>{ alpha(), alpha() };
#endif

    using Shape = ShapeOf<Plain<decltype(kCatalog)>, 2, 2, 1, 1>;
    inline constexpr auto kPlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<Shape>(kCatalog, kSelected)));

#if defined(PRODUCTION_CASE_LEGAL)
    static_assert(kPlan.diagnostic.code == PluginError::None, "duplicate_active: 合法对照必须组装成功");
#else
    ID_PRODUCTION_EXPECT_ERROR(kPlan, DuplicateActive);
#endif
} // namespace
