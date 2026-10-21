#include "ProductionCaseSupport.hpp"

/**
 *  每需求命中 >=2 个 Selected 候选：AmbiguousProvider，不挑第一个、不挑最新（计划书 6.2）
 *  合法对照：两个候选都在 catalog，但只选中 Beta。
 */

namespace
{
    using namespace ID::Bench::Production;

    inline constexpr auto kCatalog = std::tuple{
        entry_leaf(beta()),
        make_entry(gamma(), std::tuple<>{}, std::array<std::string_view, 0>{}, kTargetGamma, kNamespaceGamma,
                   provides1(kCapBeta, 1, kNoSlot)),
        make_entry(alpha(), std::tuple{ group2(kNameBeta, kNameGamma) },
                   std::array<std::string_view, 1>{ kCapBeta }, kTargetAlpha, kNamespaceAlpha,
                   provides1(kCapAlpha, 1, 0)) };

#if defined(PRODUCTION_CASE_LEGAL)
    inline constexpr auto kSelected = std::array<PluginIdentity, 2>{ alpha(), beta() };
#else
    inline constexpr auto kSelected = std::array<PluginIdentity, 3>{ alpha(), beta(), gamma() };
#endif

#if defined(PRODUCTION_CASE_LEGAL)
    using Shape = ShapeOf<Plain<decltype(kCatalog)>, 2, 2, 1, 1>;
#else
    using Shape = ShapeOf<Plain<decltype(kCatalog)>, 3, 3, 1, 1>;
#endif
    inline constexpr auto kPlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<Shape>(kCatalog, kSelected)));

#if defined(PRODUCTION_CASE_LEGAL)
    static_assert(kPlan.diagnostic.code == PluginError::None, "ambiguous_provider: 合法对照必须组装成功");
    static_assert(kPlan.edge_count == 1, "ambiguous_provider: 合法对照只有一条边");
#else
    ID_PRODUCTION_EXPECT_ERROR(kPlan, AmbiguousProvider);
#endif
} // namespace
