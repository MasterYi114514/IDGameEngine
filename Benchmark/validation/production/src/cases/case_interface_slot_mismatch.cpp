#include "ProductionCaseSupport.hpp"

/**
 *  interface 插件不得占用服务槽位（计划书 11.1 / 12.2）
 *  合法对照：interface + none 且 provides 无槽位（甚至没有 provides）。
 */

namespace
{
    using namespace ID::Bench::Production;

    inline constexpr auto kSelected = std::array<PluginIdentity, 1>{ alpha() };

#if defined(PRODUCTION_CASE_LEGAL)
    inline constexpr auto kCatalog = std::tuple{
        make_entry<1>(alpha(), std::tuple<>{}, std::array<std::string_view, 0>{}, kTargetAlpha, kNamespaceAlpha,
                      provides1(kCapAlpha, 1, kNoSlot), PluginKind::Interface, PluginLifecycle::None) };

    using Shape = ShapeOf<Plain<decltype(kCatalog)>, 1, 1, 1, 1>;
    inline constexpr auto kPlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<Shape>(kCatalog, kSelected)));

    static_assert(kPlan.diagnostic.code == PluginError::None, "interface_slot_mismatch: 无槽位能力必须合法");
    static_assert(kPlan.provision_slot[0] == kNoSlot, "interface_slot_mismatch: 纯头文件能力必须记录 kNoSlot");
#else
    inline constexpr auto kCatalog = std::tuple{
        make_entry<1>(alpha(), std::tuple<>{}, std::array<std::string_view, 0>{}, kTargetAlpha, kNamespaceAlpha,
                      provides1(kCapAlpha, 1, 0), PluginKind::Interface, PluginLifecycle::None) };

    using Shape = ShapeOf<Plain<decltype(kCatalog)>, 1, 1, 1, 1>;
    inline constexpr auto kPlan = ID::PluginManager::sort_and_validate(make_input<Shape>(kCatalog, kSelected));

    ID_PRODUCTION_EXPECT_ERROR(kPlan, InterfaceLifecycleMismatch);
#endif
} // namespace
