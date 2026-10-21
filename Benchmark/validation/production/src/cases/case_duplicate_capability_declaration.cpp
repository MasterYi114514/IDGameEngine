#include "ProductionCaseSupport.hpp"

/**
 *  同一插件重复声明同一能力必须拒绝（声明阶段，不是产品级唯一性）
 *  合法对照：同一插件声明两个不同能力。
 */

namespace
{
    using namespace ID::Bench::Production;

#if defined(PRODUCTION_CASE_LEGAL)
    inline constexpr auto kCatalog = std::tuple{
        make_entry<2>(alpha(), std::tuple<>{}, std::array<std::string_view, 0>{}, kTargetAlpha, kNamespaceAlpha,
                      std::array<Provision, 2>{ { provision(kCapAlpha, 1, 0), provision(kCapBeta, 1, kNoSlot) } }) };
#else
    inline constexpr auto kCatalog = std::tuple{
        make_entry<2>(alpha(), std::tuple<>{}, std::array<std::string_view, 0>{}, kTargetAlpha, kNamespaceAlpha,
                      std::array<Provision, 2>{ { provision(kCapAlpha, 1, 0), provision(kCapAlpha, 2, kNoSlot) } }) };
#endif

    inline constexpr auto kSelected = std::array<PluginIdentity, 1>{ alpha() };
    using Shape                     = ShapeOf<Plain<decltype(kCatalog)>, 1, 1, 1, 1>;
    inline constexpr auto kPlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<Shape>(kCatalog, kSelected)));

#if defined(PRODUCTION_CASE_LEGAL)
    static_assert(kPlan.diagnostic.code == PluginError::None, "duplicate_capability_declaration: 合法对照必须组装成功");
    static_assert(kPlan.provision_count == 2, "duplicate_capability_declaration: 两条不同能力都要保留");
#else
    ID_PRODUCTION_EXPECT_ERROR(kPlan, DuplicateCapability);
#endif
} // namespace
