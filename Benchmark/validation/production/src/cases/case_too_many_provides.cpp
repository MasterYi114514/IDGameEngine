#include "ProductionCaseSupport.hpp"

/**
 *  provides 容量由条目类型 P 给出，不是全局固定值：provides_count 超过 P 必须结构化拒绝
 *  合法对照：P = 1、provides_count = 1。
 */

namespace
{
    using namespace ID::Bench::Production;

#if defined(PRODUCTION_CASE_LEGAL)
    inline constexpr auto kCatalog = std::tuple{
        make_entry(alpha(), std::tuple<>{}, std::array<std::string_view, 0>{}, kTargetAlpha, kNamespaceAlpha,
                   provides1(kCapAlpha, 1, 0)) };
#else
    consteval auto make_catalog() noexcept
    {
        auto alpha_entry = make_entry(alpha(), std::tuple<>{}, std::array<std::string_view, 0>{}, kTargetAlpha,
                                      kNamespaceAlpha, provides1(kCapAlpha, 1, 0));
        alpha_entry.metadata.provides_count = 2;      // P = 1，声明 2 条
        return std::tuple{ alpha_entry };
    }

    inline constexpr auto kCatalog = make_catalog();
#endif

    inline constexpr auto kSelected = std::array<PluginIdentity, 1>{ alpha() };
    using Shape                     = ShapeOf<Plain<decltype(kCatalog)>, 1, 1, 1, 1>;
    inline constexpr auto kPlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<Shape>(kCatalog, kSelected)));

#if defined(PRODUCTION_CASE_LEGAL)
    static_assert(kPlan.diagnostic.code == PluginError::None, "too_many_provides: 合法对照必须组装成功");
#else
    ID_PRODUCTION_EXPECT_ERROR(kPlan, TooManyProvides);
#endif
} // namespace
