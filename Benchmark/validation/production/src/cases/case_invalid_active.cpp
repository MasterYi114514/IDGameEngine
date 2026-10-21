#include "ProductionCaseSupport.hpp"

/**
 *  非法 active 身份：选中集合里出现格式非法的身份必须拒绝（不是崩溃）
 *  合法对照：同一位置使用合法身份。
 */

namespace
{
    using namespace ID::Bench::Production;

    inline constexpr auto kCatalog = std::tuple{ entry_leaf(alpha()) };

#if defined(PRODUCTION_CASE_LEGAL)
    inline constexpr auto kSelected = std::array<PluginIdentity, 1>{ alpha() };
#else
    inline constexpr auto kSelected = std::array<PluginIdentity, 1>{ ID::PluginManager::register_plugin("1Bad") };
#endif

    using Shape = ShapeOf<Plain<decltype(kCatalog)>, 1, 1, 1, 1>;
    inline constexpr auto kPlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<Shape>(kCatalog, kSelected)));

#if defined(PRODUCTION_CASE_LEGAL)
    static_assert(kPlan.diagnostic.code == PluginError::None, "invalid_active: 合法对照必须组装成功");
#else
    static_assert(!ID::PluginManager::register_plugin("1Bad").is_valid, "invalid_active: 夹具身份必须非法");
    ID_PRODUCTION_EXPECT_ERROR(kPlan, InvalidActive);
#endif
} // namespace
