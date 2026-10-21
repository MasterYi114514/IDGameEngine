#include "ProductionCaseSupport.hpp"

/**
 *  不同 capability 占用同一个服务槽位必须拒绝（计划书 6.2 / 11.1）
 *  合法对照：两个插件占不同槽位。
 */

namespace
{
    using namespace ID::Bench::Production;

#if defined(PRODUCTION_CASE_LEGAL)
    inline constexpr int16_t kGammaSlot = 2;
#else
    inline constexpr int16_t kGammaSlot = 1;
#endif

    inline constexpr auto kCatalog = std::tuple{
        make_entry(beta(), std::tuple<>{}, std::array<std::string_view, 0>{}, kTargetBeta, kNamespaceBeta,
                   provides1(kCapBeta, 1, 1)),
        make_entry(gamma(), std::tuple<>{}, std::array<std::string_view, 0>{}, kTargetGamma, kNamespaceGamma,
                   provides1(kCapGamma, 1, kGammaSlot)) };
    inline constexpr auto kSelected = std::array<PluginIdentity, 2>{ beta(), gamma() };
    using Shape                     = ShapeOf<Plain<decltype(kCatalog)>, 2, 2, 1, 1>;
    inline constexpr auto kPlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<Shape>(kCatalog, kSelected)));

#if defined(PRODUCTION_CASE_LEGAL)
    static_assert(kPlan.diagnostic.code == PluginError::None, "slot_conflict: 合法对照必须组装成功");
    static_assert(kPlan.slot_owner[1] == kPlan.find_active("ID.Case.Beta"), "slot_conflict: 槽 1 归属 Beta");
    static_assert(kPlan.slot_owner[2] == kPlan.find_active("ID.Case.Gamma"), "slot_conflict: 槽 2 归属 Gamma");
#else
    ID_PRODUCTION_EXPECT_ERROR(kPlan, SlotConflict);
#endif
} // namespace
