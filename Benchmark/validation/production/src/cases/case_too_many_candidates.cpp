#include "ProductionCaseSupport.hpp"

/**
 *  候选数上限：每组最多 16 个候选，第 17 个必须拒绝（计划书 6.5）
 *  合法对照：恰好 16 个不同候选，全部在 catalog 且都提供该需求能力；只选中其中一个。
 */

namespace
{
    using namespace ID::Bench::Production;

#if defined(PRODUCTION_CASE_LEGAL)
    inline constexpr std::array<std::string_view, 16> kGroup16{
        kNameBeta, generated_name(0), generated_name(1), generated_name(2),
        generated_name(3), generated_name(4), generated_name(5), generated_name(6),
        generated_name(7), generated_name(8), generated_name(9), generated_name(10),
        generated_name(11), generated_name(12), generated_name(13), generated_name(14) };

    inline constexpr auto kCatalog = std::tuple_cat(
        std::tuple{
            entry_leaf(beta()),
            make_entry(alpha(), std::tuple{ kGroup16 }, std::array<std::string_view, 1>{ kCapBeta },
                       kTargetAlpha, kNamespaceAlpha, provides1(kCapAlpha, 1, 0)) },
        generated_cap_leaf_tuple<15>(kCapBeta));
    inline constexpr auto kSelected = std::array<PluginIdentity, 2>{ alpha(), beta() };
#else
    inline constexpr std::array<std::string_view, 17> kGroup17{
        kNameBeta, kNameBeta, kNameBeta, kNameBeta, kNameBeta,
        kNameBeta, kNameBeta, kNameBeta, kNameBeta, kNameBeta,
        kNameBeta, kNameBeta, kNameBeta, kNameBeta, kNameBeta,
        kNameBeta, kNameBeta };

    inline constexpr auto kCatalog = std::tuple{
        entry_leaf(beta()),
        make_entry(alpha(), std::tuple{ kGroup17 }, std::array<std::string_view, 1>{ kCapBeta },
                   kTargetAlpha, kNamespaceAlpha, provides1(kCapAlpha, 1, 0)) };
    inline constexpr auto kSelected = std::array<PluginIdentity, 2>{ alpha(), beta() };
#endif

    using Shape = ShapeOf<Plain<decltype(kCatalog)>, 2, 2, 1, 1>;

    inline constexpr auto kPlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<Shape>(kCatalog, kSelected)));

#if defined(PRODUCTION_CASE_LEGAL)
    static_assert(kPlan.diagnostic.code == PluginError::None, "too_many_candidates: 16 个候选必须合法");
    static_assert(kPlan.edge_count == 1, "too_many_candidates: 合法对照只有一条真实边");
#else
    ID_PRODUCTION_EXPECT_ERROR(kPlan, TooManyCandidates);
#endif
} // namespace
