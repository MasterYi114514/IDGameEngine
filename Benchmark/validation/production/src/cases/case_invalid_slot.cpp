#include "ProductionCaseSupport.hpp"

/**
 *  槽位边界：0..63 合法，64 必须拒绝（计划书 7 槽位）
 *  合法对照：同一插件占槽 63，并检查 slot_owner[63] 落到该插件。
 */

namespace
{
    using namespace ID::Bench::Production;

#if defined(PRODUCTION_CASE_LEGAL)
    inline constexpr auto kCatalog = std::tuple{
        make_entry(alpha(), std::tuple<>{}, std::array<std::string_view, 0>{}, kTargetAlpha, kNamespaceAlpha,
                   provides1(kCapAlpha, 1, 63)) };
#else
    inline constexpr auto kCatalog = std::tuple{
        make_entry(alpha(), std::tuple<>{}, std::array<std::string_view, 0>{}, kTargetAlpha, kNamespaceAlpha,
                   provides1(kCapAlpha, 1, 64)) };
#endif

    inline constexpr auto kSelected = std::array<PluginIdentity, 1>{ alpha() };
    using Shape                     = ShapeOf<Plain<decltype(kCatalog)>, 1, 1, 1, 1>;
    inline constexpr auto kPlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<Shape>(kCatalog, kSelected)));

#if defined(PRODUCTION_CASE_LEGAL)
    static_assert(kPlan.diagnostic.code == PluginError::None, "invalid_slot: 槽 63 必须合法");
    static_assert(kPlan.slot_owner[63] == kPlan.find_active("ID.Case.Alpha"), "invalid_slot: 槽 63 归属 Alpha");
    static_assert(kPlan.slot_owner[0] == kNoIndex, "invalid_slot: 未占用的槽必须是 kNoIndex");
#else
    ID_PRODUCTION_EXPECT_ERROR(kPlan, InvalidSlot);
#endif
} // namespace
