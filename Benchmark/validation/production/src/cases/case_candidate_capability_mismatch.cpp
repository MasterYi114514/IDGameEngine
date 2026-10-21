#include "ProductionCaseSupport.hpp"

/**
 *  候选没有声明提供该需求的能力：白名单必须与能力一致（计划书 4.4 / 7）
 *  合法对照：候选 Beta 提供 CapB。
 */

namespace
{
    using namespace ID::Bench::Production;

#if defined(PRODUCTION_CASE_LEGAL)
    inline constexpr auto kCatalog = std::tuple{ entry_leaf(beta()), entry_dep(alpha(), kNameBeta) };
#else
    inline constexpr auto kCatalog = std::tuple{
        make_entry(beta(), std::tuple<>{}, std::array<std::string_view, 0>{}, kTargetBeta, kNamespaceBeta,
                   provides1(kCapGamma, 1, 1)),
        entry_dep(alpha(), kNameBeta) };
#endif

    inline constexpr auto kSelected = std::array<PluginIdentity, 2>{ alpha(), beta() };
    using Shape                     = ShapeOf<Plain<decltype(kCatalog)>, 2, 2, 1, 1>;
    inline constexpr auto kPlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<Shape>(kCatalog, kSelected)));

#if defined(PRODUCTION_CASE_LEGAL)
    static_assert(kPlan.diagnostic.code == PluginError::None, "candidate_capability_mismatch: 合法对照必须组装成功");
#else
    ID_PRODUCTION_EXPECT_ERROR(kPlan, CandidateCapabilityMismatch);
#endif
} // namespace
