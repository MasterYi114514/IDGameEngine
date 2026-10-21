#include "ProductionCaseSupport.hpp"

/**
 *  同一 capability 的两个提供者即使槽位不同也必须拒绝（计划书 6.2）
 *  合法对照：两个插件提供不同 capability、不同槽位。
 */

namespace
{
    using namespace ID::Bench::Production;

#if defined(PRODUCTION_CASE_LEGAL)
    inline constexpr auto kGammaCapability = kCapGamma;
#else
    inline constexpr auto kGammaCapability = kCapBeta;
#endif

    inline constexpr auto kCatalog = std::tuple{
        make_entry(beta(), std::tuple<>{}, std::array<std::string_view, 0>{}, kTargetBeta, kNamespaceBeta,
                   provides1(kCapBeta, 1, 1)),
        make_entry(gamma(), std::tuple<>{}, std::array<std::string_view, 0>{}, kTargetGamma, kNamespaceGamma,
                   provides1(kGammaCapability, 1, 2)) };
    inline constexpr auto kSelected = std::array<PluginIdentity, 2>{ beta(), gamma() };
    using Shape                     = ShapeOf<Plain<decltype(kCatalog)>, 2, 2, 1, 1>;
    inline constexpr auto kPlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<Shape>(kCatalog, kSelected)));

#if defined(PRODUCTION_CASE_LEGAL)
    static_assert(kPlan.diagnostic.code == PluginError::None, "duplicate_capability_slots: 合法对照必须组装成功");
#else
    ID_PRODUCTION_EXPECT_ERROR(kPlan, DuplicateCapability);
#endif
} // namespace
