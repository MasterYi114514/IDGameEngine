#include "ProductionCaseSupport.hpp"

/**
 *  每需求命中 0 个 Selected 候选：MissingProvider，不自动启用（计划书 6.2）
 *  合法对照：同样的 catalog，把 Beta 也选中，恰好命中 1 个。
 */

namespace
{
    using namespace ID::Bench::Production;

    inline constexpr auto kCatalog = std::tuple{ entry_leaf(beta()), entry_dep(alpha(), kNameBeta) };

#if defined(PRODUCTION_CASE_LEGAL)
    inline constexpr auto kSelected = std::array<PluginIdentity, 2>{ alpha(), beta() };
#else
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
    static_assert(kPlan.diagnostic.code == PluginError::None, "missing_provider: 合法对照必须组装成功");
    static_assert(kPlan.candidate_match_count == 0, "missing_provider: 成功绑定不保留歧义诊断数据");
#else
    ID_PRODUCTION_EXPECT_ERROR(kPlan, MissingProvider);
#endif
} // namespace
