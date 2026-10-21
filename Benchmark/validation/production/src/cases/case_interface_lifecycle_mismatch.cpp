#include "ProductionCaseSupport.hpp"

/**
 *  interface 插件必须是 lifecycle=none 且没有槽位（计划书 12.2）
 *  合法对照：interface + none + 无 provides。
 */

namespace
{
    using namespace ID::Bench::Production;

    inline constexpr auto kSelected = std::array<PluginIdentity, 1>{ alpha() };

#if defined(PRODUCTION_CASE_LEGAL)
    inline constexpr auto kCatalog = std::tuple{
        make_entry<0>(alpha(), std::tuple<>{}, std::array<std::string_view, 0>{}, kTargetAlpha, kNamespaceAlpha,
                      std::array<Provision, 0>{}, PluginKind::Interface, PluginLifecycle::None) };

    using Shape = ShapeOf<Plain<decltype(kCatalog)>, 1, 1, 1, 1>;
    inline constexpr auto kPlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<Shape>(kCatalog, kSelected)));

    static_assert(kPlan.diagnostic.code == PluginError::None, "interface_lifecycle_mismatch: 合法 interface 必须组装成功");
    static_assert(kPlan.provision_count == 0, "interface_lifecycle_mismatch: interface 不得有运行期能力记录");
#else
    inline constexpr auto kCatalog = std::tuple{
        make_entry<0>(alpha(), std::tuple<>{}, std::array<std::string_view, 0>{}, kTargetAlpha, kNamespaceAlpha,
                      std::array<Provision, 0>{}, PluginKind::Interface, PluginLifecycle::Standard) };

    using Shape = ShapeOf<Plain<decltype(kCatalog)>, 1, 1, 1, 1>;
    inline constexpr auto kPlan = ID::PluginManager::sort_and_validate(make_input<Shape>(kCatalog, kSelected));

    ID_PRODUCTION_EXPECT_ERROR(kPlan, InterfaceLifecycleMismatch);
#endif
} // namespace
