#include "ProductionCaseSupport.hpp"

/**
 *  CMake 目标名非法：首字符必须是字母（计划书 7 格式校验）
 *  合法对照：同一插件使用合法 target。
 */

namespace
{
    using namespace ID::Bench::Production;

#if defined(PRODUCTION_CASE_LEGAL)
    inline constexpr auto kCatalog = std::tuple{
        make_entry(alpha(), std::tuple<>{}, std::array<std::string_view, 0>{}, kTargetAlpha, kNamespaceAlpha,
                   provides1(kCapAlpha, 1, 0)) };
#else
    inline constexpr auto kCatalog = std::tuple{
        make_entry(alpha(), std::tuple<>{}, std::array<std::string_view, 0>{}, "1BadTarget", kNamespaceAlpha,
                   provides1(kCapAlpha, 1, 0)) };
#endif

    inline constexpr auto kSelected = std::array<PluginIdentity, 1>{ alpha() };

    using Shape = ShapeOf<Plain<decltype(kCatalog)>, 1, 1, 1, 1>;

    inline constexpr auto kPlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<Shape>(kCatalog, kSelected)));

#if defined(PRODUCTION_CASE_LEGAL)
    static_assert(kPlan.diagnostic.code == PluginError::None, "invalid_target: 合法对照必须组装成功");
#else
    ID_PRODUCTION_EXPECT_ERROR(kPlan, InvalidTargetName);
#endif
} // namespace
