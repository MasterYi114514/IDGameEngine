#include "ProductionCaseSupport.hpp"

/**
 *  一旧一新两个候选都被启用：仍是 AmbiguousProvider，不自动选新（计划书 6.2）
 *  合法对照：只选中修订达标的新候选，修订区间 2..2。
 */

namespace
{
    using namespace ID::Bench::Production;

#if defined(PRODUCTION_CASE_LEGAL)
    inline constexpr auto kSelected = std::array<PluginIdentity, 2>{ alpha(), gamma() };
#else
    inline constexpr auto kSelected = std::array<PluginIdentity, 3>{ alpha(), beta(), gamma() };
#endif

    consteval auto make_catalog() noexcept
    {
        auto alpha_entry = make_entry(alpha(), std::tuple{ group2(kNameBeta, kNameGamma) },
                                      std::array<std::string_view, 1>{ kCapBeta }, kTargetAlpha, kNamespaceAlpha,
                                      provides1(kCapAlpha, 1, 0));
        alpha_entry.metadata.revisions[0] = CandidateRevision{ kNameBeta, 1, 1 };
        alpha_entry.metadata.revisions[1] = CandidateRevision{ kNameGamma, 2, 2 };
        return std::tuple{
            make_entry(beta(), std::tuple<>{}, std::array<std::string_view, 0>{}, kTargetBeta, kNamespaceBeta,
                       provides1(kCapBeta, 1, 1)),
            make_entry(gamma(), std::tuple<>{}, std::array<std::string_view, 0>{}, kTargetGamma, kNamespaceGamma,
                       provides1(kCapBeta, 2, kNoSlot)),
            alpha_entry };
    }

    inline constexpr auto kCatalog = make_catalog();

#if defined(PRODUCTION_CASE_LEGAL)
    using Shape = ShapeOf<Plain<decltype(kCatalog)>, 2, 2, 1, 1>;
#else
    using Shape = ShapeOf<Plain<decltype(kCatalog)>, 3, 3, 1, 1>;
#endif

    inline constexpr auto kPlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<Shape>(kCatalog, kSelected)));

#if defined(PRODUCTION_CASE_LEGAL)
    static_assert(kPlan.diagnostic.code == PluginError::None, "ambiguous_old_new: 合法对照必须组装成功");
    static_assert(kPlan.candidate_match_count == 0, "ambiguous_old_new: 成功绑定不保留歧义诊断数据");
#else
    ID_PRODUCTION_EXPECT_ERROR(kPlan, AmbiguousProvider);
#endif
} // namespace
