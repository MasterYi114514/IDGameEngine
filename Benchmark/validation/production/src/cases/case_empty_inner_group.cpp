#include "ProductionCaseSupport.hpp"

/**
 *  空内层组：`[[]]` 不是合法需求（计划书 5.3）
 *  合法对照：同一插件改成单候选组。
 */

namespace
{
    using namespace ID::Bench::Production;

#if defined(PRODUCTION_CASE_LEGAL)
    inline constexpr auto kCatalog = std::tuple{
        entry_leaf(beta()),
        make_entry(alpha(), std::tuple{ group1(kNameBeta) }, std::array<std::string_view, 1>{ kCapBeta },
                   kTargetAlpha, kNamespaceAlpha, provides1(kCapAlpha, 1, 0)) };
    inline constexpr auto kSelected = std::array<PluginIdentity, 2>{ alpha(), beta() };
#else
    inline constexpr auto kCatalog = std::tuple{
        make_entry(alpha(), std::tuple{ group0() }, std::array<std::string_view, 1>{ kCapBeta },
                   kTargetAlpha, kNamespaceAlpha, provides1(kCapAlpha, 1, 0)) };
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
    static_assert(kPlan.diagnostic.code == PluginError::None, "empty_inner_group: 合法对照必须组装成功");
#else
    ID_PRODUCTION_EXPECT_ERROR(kPlan, EmptyRequirementGroup);
#endif
} // namespace
