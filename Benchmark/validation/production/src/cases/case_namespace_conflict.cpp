#include "ProductionCaseSupport.hpp"

/**
 *  C++ 命名空间冲突：两个插件声明同一个精确 ASCII 命名空间必须拒绝
 *  合法对照：两个插件命名空间不同。
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
    inline constexpr auto kCatalog = std::tuple{
        make_entry(alpha(), std::tuple<>{}, std::array<std::string_view, 0>{}, kTargetAlpha, kNamespaceAlpha,
                   provides1(kCapAlpha, 1, 0)),
        make_entry(beta(), std::tuple<>{}, std::array<std::string_view, 0>{}, kTargetBeta, kNamespaceAlpha,
                   provides1(kCapBeta, 1, 1)) };
#endif

    inline constexpr auto kSelected = std::array<PluginIdentity, 2>{ alpha(), beta() };

    using Shape = ShapeOf<Plain<decltype(kCatalog)>, 2, 2, 1, 1>;

    inline constexpr auto kPlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<Shape>(kCatalog, kSelected)));

#if defined(PRODUCTION_CASE_LEGAL)
    static_assert(kPlan.diagnostic.code == PluginError::None, "namespace_conflict: 合法对照必须组装成功");
#else
    ID_PRODUCTION_EXPECT_ERROR(kPlan, NamespaceConflict);
#endif
} // namespace
