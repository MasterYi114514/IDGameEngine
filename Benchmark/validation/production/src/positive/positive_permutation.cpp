#include "ProductionCaseSupport.hpp"
#include "ProductionPositiveProbes.hpp"

/**
 *  正向对照：打乱 catalog 顺序、selected 顺序、候选顺序、provides 顺序后，
 *  canonical active 表、逐需求 provider、拓扑序、扁平 provision 与指纹必须完全相同。
 *  编译期断言 + 运行期探针：TU 必须进入 ProductionCaseControls 的链接行并能被调用。
 */

namespace
{
    using namespace ID::Bench::Production;

    consteval auto make_alpha(bool swapped) noexcept
    {
        const auto declaration = swapped
            ? ID::PluginManager::declare_dependencies(alpha(), std::tuple{ group2(kNameDelta, kNameBeta) })
            : ID::PluginManager::declare_dependencies(alpha(), std::tuple{ group2(kNameBeta, kNameDelta) });
        const std::array<std::string_view, 2> revision_names{ kNameBeta, kNameDelta };
        const std::array<Provision, 2>        provides = swapped
            ? std::array<Provision, 2>{ { provision(kCapZeta, 2, kNoSlot), provision(kCapAlpha, 1, 0) } }
            : std::array<Provision, 2>{ { provision(kCapAlpha, 1, 0), provision(kCapZeta, 2, kNoSlot) } };
        return entry_from_declaration(declaration, std::array<std::string_view, 1>{ kCapBeta }, revision_names,
                                      kTargetAlpha, kNamespaceAlpha, provides);
    }

    consteval auto make_delta() noexcept
    {
        return make_entry(delta(), std::tuple<>{}, std::array<std::string_view, 0>{}, kTargetDelta, kNamespaceDelta,
                          provides1(kCapBeta, 1, kNoSlot));
    }

    inline constexpr auto kCatalogA = std::tuple{ make_alpha(false), entry_leaf(beta()), make_delta() };
    inline constexpr auto kSelectedA = std::array<PluginIdentity, 2>{ beta(), alpha() };

    inline constexpr auto kCatalogB = std::tuple{ make_delta(), entry_leaf(beta()), make_alpha(true) };
    inline constexpr auto kSelectedB = std::array<PluginIdentity, 2>{ alpha(), beta() };

    using ShapeA                  = ShapeOf<Plain<decltype(kCatalogA)>, 2, 2, 1, 1>;
    using ShapeB                  = ShapeOf<Plain<decltype(kCatalogB)>, 2, 2, 1, 1>;
    inline constexpr auto kPlanA = ID::PluginManager::sort_and_validate(with_evidence(make_input<ShapeA>(kCatalogA, kSelectedA)));
    inline constexpr auto kPlanB = ID::PluginManager::sort_and_validate(with_evidence(make_input<ShapeB>(kCatalogB, kSelectedB)));

    static_assert(kPlanA.diagnostic.code == PluginError::None, "positive_permutation: A 变体必须合法");
    static_assert(kPlanB.diagnostic.code == PluginError::None, "positive_permutation: B 变体必须合法");
    static_assert(kPlanA.fingerprint == kPlanB.fingerprint, "positive_permutation: 指纹必须与书写顺序无关");
    static_assert(kPlanA.active_identity[0].view() == "ID.Case.Alpha" &&
                      kPlanB.active_identity[0].view() == "ID.Case.Alpha",
                  "positive_permutation: canonical 首项必须都是 Alpha");
    static_assert(kPlanA.provider[0] == kPlanA.find_active("ID.Case.Beta") &&
                      kPlanB.provider[0] == kPlanB.find_active("ID.Case.Beta"),
                  "positive_permutation: 逐需求 provider 必须相同");
    static_assert(kPlanA.topo_order[0] == kPlanA.find_active("ID.Case.Beta") &&
                      kPlanB.topo_order[0] == kPlanB.find_active("ID.Case.Beta"),
                  "positive_permutation: 拓扑序必须相同");
    static_assert(kPlanA.provision_count == 3 && kPlanB.provision_count == 3,
                  "positive_permutation: provides 书写顺序不得改变记录条数");
    static_assert(kPlanA.provision_capability_view(0) == "ID.Case.CapA" &&
                      kPlanB.provision_capability_view(0) == "ID.Case.CapA",
                  "positive_permutation: 扁平表必须按能力名规范化");
    static_assert(kPlanA.provision_slot[0] == 0 && kPlanB.provision_slot[0] == 0,
                  "positive_permutation: 同一条能力必须保留同一槽位");
} // namespace

namespace ID::Bench::Production::Positive
{
    ProbeResult permutation() noexcept
    {
        using namespace ID::Bench::Production;
        return ProbeResult{ kPlanA.active_count, kPlanA.edge_count, kPlanA.provision_count,
                            static_cast<uint8_t>(kPlanA.diagnostic.code) };
    }
} // namespace ID::Bench::Production::Positive
