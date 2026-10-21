#include "ProductionCaseSupport.hpp"
#include "ProductionPositiveProbes.hpp"

/**
 *  正向对照：不等长 tuple-of-arrays、空外层、矩形重载必须都能表达
 *  1. tuple 形态：2 组，组大小分别为 1 和 3；另一个插件声明空外层；
 *  2. 矩形形态：2 组 × 2 候选，同一上游同时满足两条需求，图边去重为 1。
 *  编译期断言 + 运行期探针：TU 必须进入 ProductionCaseControls 的链接行并能被调用。
 */

namespace
{
    using namespace ID::Bench::Production;

    // ---- tuple 不等长 + 空外层 ----
    inline constexpr auto kTupleCatalog = std::tuple{
        entry_leaf(beta()),
        make_entry(gamma(), std::tuple<>{}, std::array<std::string_view, 0>{}, kTargetGamma, kNamespaceGamma,
                   provides1(kCapGamma, 1, 2)),
        make_entry(delta(), std::tuple<>{}, std::array<std::string_view, 0>{}, kTargetDelta, kNamespaceDelta,
                   provides1(kCapGamma, 1, 3)),
        make_entry(zeta(), std::tuple<>{}, std::array<std::string_view, 0>{}, kTargetZeta, kNamespaceZeta,
                   provides1(kCapGamma, 1, 4)),
        entry_leaf(theta()),
        make_entry(alpha(), std::tuple{ group1(kNameBeta), group3(kNameGamma, kNameDelta, kNameZeta) },
                   std::array<std::string_view, 2>{ kCapBeta, kCapGamma }, kTargetAlpha, kNamespaceAlpha,
                   provides1(kCapAlpha, 1, 0)) };

    inline constexpr auto kTupleSelected = std::array<PluginIdentity, 4>{ alpha(), beta(), gamma(), theta() };
    using TupleShape                     = ShapeOf<Plain<decltype(kTupleCatalog)>, 4, 4, 2, 2, 4>;
    inline constexpr auto kTuplePlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<TupleShape>(kTupleCatalog, kTupleSelected)));

    static_assert(kTuplePlan.diagnostic.code == PluginError::None, "positive_uneven: 不等长 tuple 必须合法");
    static_assert(kTuplePlan.active_requirement_count[kTuplePlan.find_active("ID.Case.Alpha")] == 2,
                  "positive_uneven: Alpha 必须保留两组需求");
    static_assert(kTuplePlan.active_requirement_count[kTuplePlan.find_active("ID.Case.Theta")] == 0,
                  "positive_uneven: 空外层必须表示没有上游");
    static_assert(kTuplePlan.edge_count == 2, "positive_uneven: 两条需求各绑定一个不同上游");
    static_assert(kTuplePlan.provider[kTuplePlan.find_active("ID.Case.Alpha") * 32 + 1] ==
                      kTuplePlan.find_active("ID.Case.Gamma"),
                  "positive_uneven: 第二组必须绑定 Gamma");

    // ---- 矩形重载：2 组 × 2 候选 ----
    constexpr std::array<std::array<std::string_view, 2>, 2> kRectGroups{
        { { kNameBeta, kNameGamma }, { kNameBeta, kNameDelta } } };

    consteval auto make_rect_catalog() noexcept
    {
        const auto declaration = ID::PluginManager::declare_dependencies(alpha(), kRectGroups);
        return std::tuple{
            entry_leaf(beta()),
            make_entry(gamma(), std::tuple<>{}, std::array<std::string_view, 0>{}, kTargetGamma, kNamespaceGamma,
                       provides1(kCapBeta, 1, kNoSlot)),
            make_entry(delta(), std::tuple<>{}, std::array<std::string_view, 0>{}, kTargetDelta, kNamespaceDelta,
                       provides1(kCapBeta, 1, kNoSlot)),
            entry_from_declaration(declaration, std::array<std::string_view, 2>{ kCapBeta, kCapBeta },
                                   std::array<std::string_view, 4>{ kNameBeta, kNameGamma, kNameBeta, kNameDelta },
                                   kTargetAlpha, kNamespaceAlpha, provides1(kCapAlpha, 1, 0)) };
    }

    inline constexpr auto kRectCatalog  = make_rect_catalog();
    inline constexpr auto kRectSelected = std::array<PluginIdentity, 2>{ alpha(), beta() };
    using RectShape                     = ShapeOf<Plain<decltype(kRectCatalog)>, 2, 2, 2, 1>;
    inline constexpr auto kRectPlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<RectShape>(kRectCatalog, kRectSelected)));

    static_assert(kRectPlan.diagnostic.code == PluginError::None, "positive_uneven: 矩形重载必须合法");
    static_assert(kRectPlan.active_requirement_count[kRectPlan.find_active("ID.Case.Alpha")] == 2,
                  "positive_uneven: 矩形重载必须产生两组需求");
    static_assert(kRectPlan.edge_count == 1, "positive_uneven: 同一上游满足两条需求时边只计一次");
    static_assert(kRectPlan.provider[0] == kRectPlan.find_active("ID.Case.Beta") &&
                      kRectPlan.provider[1] == kRectPlan.find_active("ID.Case.Beta"),
                  "positive_uneven: 两条需求都必须绑定 Beta");
} // namespace

namespace ID::Bench::Production::Positive
{
    ProbeResult uneven() noexcept
    {
        using namespace ID::Bench::Production;
        return ProbeResult{ kTuplePlan.active_count, kTuplePlan.edge_count, kTuplePlan.provision_count,
                            static_cast<uint8_t>(kTuplePlan.diagnostic.code) };
    }
} // namespace ID::Bench::Production::Positive
