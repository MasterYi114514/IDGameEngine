#include "ProductionCaseSupport.hpp"
#include "ProductionPositiveProbes.hpp"

/**
 *  正向对照：同一上游满足多条需求时边只计一次；未选中后端形成的潜在环不误报
 *  编译期断言 + 运行期探针：TU 必须进入 ProductionCaseControls 的链接行并能被调用。
 */

namespace
{
    using namespace ID::Bench::Production;

    // ---- Beta 同时满足 Alpha 的两个需求：绑定 2 条、真实边 1 条 ----
    consteval auto make_two_group_catalog() noexcept
    {
        auto alpha_entry = make_entry(
            alpha(), std::tuple{ group2(kNameBeta, kNameGamma), group1(kNameBeta) },
            std::array<std::string_view, 2>{ kCapBeta, kCapBeta }, kTargetAlpha, kNamespaceAlpha,
            provides1(kCapAlpha, 1, 0));
        return std::tuple{
            entry_leaf(beta()),
            make_entry(gamma(), std::tuple<>{}, std::array<std::string_view, 0>{}, kTargetGamma, kNamespaceGamma,
                       provides1(kCapBeta, 1, kNoSlot)),
            alpha_entry };
    }

    inline constexpr auto kTwoGroupCatalog  = make_two_group_catalog();
    inline constexpr auto kTwoGroupSelected = std::array<PluginIdentity, 2>{ alpha(), beta() };
    using TwoGroupShape                     = ShapeOf<Plain<decltype(kTwoGroupCatalog)>, 2, 2, 2, 1>;
    inline constexpr auto kTwoGroupPlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<TwoGroupShape>(kTwoGroupCatalog, kTwoGroupSelected)));

    static_assert(kTwoGroupPlan.diagnostic.code == PluginError::None, "positive_graph: 共享上游图必须合法");
    static_assert(kTwoGroupPlan.active_requirement_count[kTwoGroupPlan.find_active("ID.Case.Alpha")] == 2,
                  "positive_graph: 两条需求绑定都必须保留");
    static_assert(kTwoGroupPlan.edge_count == 1, "positive_graph: 同一上游的两条需求只留一条真实边");
    static_assert(kTwoGroupPlan.provider[kTwoGroupPlan.find_active("ID.Case.Alpha") * 32 + 0] ==
                      kTwoGroupPlan.find_active("ID.Case.Beta"),
                  "positive_graph: 第 0 条需求必须绑定 Beta");
    static_assert(kTwoGroupPlan.provider[kTwoGroupPlan.find_active("ID.Case.Alpha") * 32 + 1] ==
                      kTwoGroupPlan.find_active("ID.Case.Beta"),
                  "positive_graph: 第 1 条需求必须绑定 Beta");

    // ---- catalog 里存在 A<->B 环，但两者都未选中：不得误报 Cycle ----
    inline constexpr auto kCyclicCatalog = std::tuple{
        entry_dep(alpha(), kNameBeta), entry_dep(beta(), kNameAlpha), entry_leaf(gamma()), entry_leaf(delta()) };
    inline constexpr auto kSafeSelected = std::array<PluginIdentity, 2>{ gamma(), delta() };
    using SafeShape                     = ShapeOf<Plain<decltype(kCyclicCatalog)>, 2, 2, 1, 1>;
    inline constexpr auto kSafePlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<SafeShape>(kCyclicCatalog, kSafeSelected)));

    static_assert(kSafePlan.diagnostic.code == PluginError::None, "positive_graph: 未选中后端的潜在环不得误报");
    static_assert(kSafePlan.catalog_count == 4 && kSafePlan.active_count == 2,
                  "positive_graph: 完整 catalog 与选中集合必须分别记账");
    static_assert(kSafePlan.edge_count == 0, "positive_graph: 未选中插件不产生真实边");
} // namespace

namespace ID::Bench::Production::Positive
{
    ProbeResult graph() noexcept
    {
        using namespace ID::Bench::Production;
        return ProbeResult{ kTwoGroupPlan.active_count, kTwoGroupPlan.edge_count, kTwoGroupPlan.provision_count,
                            static_cast<uint8_t>(kTwoGroupPlan.diagnostic.code) };
    }
} // namespace ID::Bench::Production::Positive
