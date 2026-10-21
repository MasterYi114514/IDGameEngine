#include "ProductionCaseSupport.hpp"

/**
 *  catalog 重名：同一插件名声明两次必须拒绝（计划书第 7 节身份校验）
 *  合法对照：两个不同名字的插件。
 */

namespace
{
    using namespace ID::Bench::Production;

#if defined(PRODUCTION_CASE_LEGAL)
    inline constexpr auto kCatalog  = std::tuple{ entry_leaf(alpha()), entry_leaf(beta()) };
    inline constexpr auto kSelected = std::array<PluginIdentity, 2>{ alpha(), beta() };
#else
    // 同名同 ID 的两个 catalog 条目：身份必须唯一
    inline constexpr auto kCatalog  = std::tuple{ entry_leaf(alpha()), entry_leaf(alpha()) };
    inline constexpr auto kSelected = std::array<PluginIdentity, 1>{ alpha() };
#endif

#if defined(PRODUCTION_CASE_LEGAL)
    using Shape = ShapeOf<Plain<decltype(kCatalog)>, 2, 2, 1, 1>;
#else
    using Shape = ShapeOf<Plain<decltype(kCatalog)>, 1, 1, 1, 1>;
#endif

    inline constexpr auto kPlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<Shape>(kCatalog, kSelected)));

#if defined(PRODUCTION_CASE_LEGAL)
    static_assert(kPlan.diagnostic.code == PluginError::None, "duplicate_plugin: 合法对照必须组装成功");
    static_assert(kPlan.active_count == 2, "duplicate_plugin: 两个不同插件必须都进入 active");
#else
    ID_PRODUCTION_EXPECT_ERROR(kPlan, DuplicatePlugin);
#endif
} // namespace
