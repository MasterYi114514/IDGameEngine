#include "ProductionCaseSupport.hpp"

/**
 *  组内重复候选：同一需求组出现同一个插件两次必须拒绝（计划书 7 依赖声明）
 *  合法对照：同一组里两个不同候选，只选中其中一个。
 */

namespace
{
    using namespace ID::Bench::Production;

#if defined(PRODUCTION_CASE_LEGAL)
    inline constexpr auto kCatalog = std::tuple{
        make_entry(beta(), std::tuple<>{}, std::array<std::string_view, 0>{}, kTargetBeta, kNamespaceBeta,
                   provides1(kCapBeta, 1, 1)),
        make_entry(delta(), std::tuple<>{}, std::array<std::string_view, 0>{}, kTargetDelta, kNamespaceDelta,
                   provides1(kCapBeta, 1, kNoSlot)),
        make_entry(alpha(), std::tuple{ group2(kNameBeta, kNameDelta) },
                   std::array<std::string_view, 1>{ kCapBeta }, kTargetAlpha, kNamespaceAlpha,
                   provides1(kCapAlpha, 1, 0)) };
    inline constexpr auto kSelected = std::array<PluginIdentity, 2>{ alpha(), beta() };
#else
    inline constexpr auto kCatalog = std::tuple{
        entry_leaf(beta()),
        make_entry(alpha(), std::tuple{ group2(kNameBeta, kNameBeta) },
                   std::array<std::string_view, 1>{ kCapBeta }, kTargetAlpha, kNamespaceAlpha,
                   provides1(kCapAlpha, 1, 0)) };
    inline constexpr auto kSelected = std::array<PluginIdentity, 2>{ alpha(), beta() };
#endif

    using Shape = ShapeOf<Plain<decltype(kCatalog)>, 2, 2, 1, 1>;

    inline constexpr auto kPlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<Shape>(kCatalog, kSelected)));

#if defined(PRODUCTION_CASE_LEGAL)
    static_assert(kPlan.diagnostic.code == PluginError::None, "duplicate_candidate: 合法对照必须组装成功");
    static_assert(kPlan.edge_count == 1, "duplicate_candidate: 合法对照只保留一条真实边");
#else
    ID_PRODUCTION_EXPECT_ERROR(kPlan, DuplicateCandidate);
#endif
} // namespace
