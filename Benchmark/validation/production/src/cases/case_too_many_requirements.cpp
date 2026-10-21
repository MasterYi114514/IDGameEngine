#include "ProductionCaseSupport.hpp"

/**
 *  需求组数上限：每插件最多 32 组，第 33 组必须拒绝（计划书 6.5 / 7）
 *  合法对照：32 组、同一上游；绑定记录保留 32 条，图边只去重成 1 条。
 */

namespace
{
    using namespace ID::Bench::Production;

#if defined(PRODUCTION_CASE_LEGAL)
    inline constexpr size_t kGroups = 32;
#else
    inline constexpr size_t kGroups = 33;
#endif

    consteval auto make_groups() noexcept
    {
        return repeated_groups<kGroups>(kNameBeta);
    }

    consteval auto make_caps() noexcept
    {
        return repeated_capabilities<kGroups>(kCapBeta);
    }

    inline constexpr auto kAlpha = make_entry(beta(), std::tuple<>{}, std::array<std::string_view, 0>{},
                                              kTargetBeta, kNamespaceBeta, provides1(kCapBeta, 1, 1));
    inline constexpr auto kCatalog = std::tuple{
        kAlpha,
        make_entry(alpha(), make_groups(), make_caps(), kTargetAlpha, kNamespaceAlpha, provides1(kCapAlpha, 1, 0)) };
    inline constexpr auto kSelected = std::array<PluginIdentity, 2>{ alpha(), beta() };

    using Shape = ShapeOf<Plain<decltype(kCatalog)>, 2, 2, kGroups, 1>;

    inline constexpr auto kPlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<Shape>(kCatalog, kSelected)));

#if defined(PRODUCTION_CASE_LEGAL)
    static_assert(kPlan.diagnostic.code == PluginError::None, "too_many_requirements: 32 组必须合法");
    static_assert(kPlan.active_requirement_count[kPlan.find_active("ID.Case.Alpha")] == 32,
                  "too_many_requirements: 合法对照必须保留 32 条需求绑定");
    static_assert(kPlan.edge_count == 1, "too_many_requirements: 同一上游的 32 条需求只留一条边");
#else
    ID_PRODUCTION_EXPECT_ERROR(kPlan, TooManyRequirements);
#endif
} // namespace
