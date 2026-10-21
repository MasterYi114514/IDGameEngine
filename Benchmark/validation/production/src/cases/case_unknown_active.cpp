#include "ProductionCaseSupport.hpp"

/**
 *  未知 active：选中集合出现 catalog 之外的插件必须拒绝（计划书 7 产品选择）
 *  合法对照：只选中 catalog 中的 Alpha。
 */

namespace
{
    using namespace ID::Bench::Production;

    inline constexpr auto kCatalog = std::tuple{ entry_leaf(alpha()) };

#if defined(PRODUCTION_CASE_LEGAL)
    inline constexpr auto kSelected = std::array<PluginIdentity, 1>{ alpha() };
#else
    inline constexpr auto kSelected = std::array<PluginIdentity, 1>{ ID::PluginManager::register_plugin(kNameGhost) };
#endif

    using Shape = ShapeOf<Plain<decltype(kCatalog)>, 1, 1, 1, 1>;
    inline constexpr auto kPlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<Shape>(kCatalog, kSelected)));

#if defined(PRODUCTION_CASE_LEGAL)
    static_assert(kPlan.diagnostic.code == PluginError::None, "unknown_active: 合法对照必须组装成功");
#else
    ID_PRODUCTION_EXPECT_ERROR(kPlan, UnknownActive);
#endif
} // namespace
