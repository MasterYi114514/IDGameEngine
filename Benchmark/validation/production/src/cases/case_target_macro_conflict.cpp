#include "ProductionCaseSupport.hpp"

/**
 *  target 归一化后的身份宏冲突：两个插件映射到同一个 ID_USE_PLUGIN_ 宏必须拒绝
 *  合法对照：两个 target 归一化后不同。
 */

namespace
{
    using namespace ID::Bench::Production;

#if defined(PRODUCTION_CASE_LEGAL)
    inline constexpr auto kCatalog = std::tuple{
        make_entry(alpha(), std::tuple<>{}, std::array<std::string_view, 0>{}, kTargetAlpha, kNamespaceAlpha,
                   provides1(kCapAlpha, 1, 0)),
        make_entry(beta(), std::tuple<>{}, std::array<std::string_view, 0>{}, kTargetBeta, kNamespaceBeta,
                   provides1(kCapBeta, 1, 1)) };
#else
    // IDCaseAlpha 与 IDCASEALPHA 归一化后都是 ID_USE_PLUGIN_IDCASEALPHA
    inline constexpr auto kCatalog = std::tuple{
        make_entry(alpha(), std::tuple<>{}, std::array<std::string_view, 0>{}, "IDCaseAlpha", kNamespaceAlpha,
                   provides1(kCapAlpha, 1, 0)),
        make_entry(beta(), std::tuple<>{}, std::array<std::string_view, 0>{}, "IDCASEALPHA", kNamespaceBeta,
                   provides1(kCapBeta, 1, 1)) };
#endif

    inline constexpr auto kSelected = std::array<PluginIdentity, 2>{ alpha(), beta() };

    using Shape = ShapeOf<Plain<decltype(kCatalog)>, 2, 2, 1, 1>;

    inline constexpr auto kPlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<Shape>(kCatalog, kSelected)));

#if defined(PRODUCTION_CASE_LEGAL)
    static_assert(kPlan.diagnostic.code == PluginError::None, "target_macro_conflict: 合法对照必须组装成功");
#else
    ID_PRODUCTION_EXPECT_ERROR(kPlan, TargetMacroConflict);
#endif
} // namespace
