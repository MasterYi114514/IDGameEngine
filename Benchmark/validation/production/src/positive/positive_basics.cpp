#include "ProductionCaseSupport.hpp"
#include "ProductionPositiveProbes.hpp"

/**
 *  正向对照：0/1 active、canonical 名字序、Kahn 的确定性 tie-break
 *  编译期断言 + 运行期探针：TU 必须进入 ProductionCaseControls 的链接行并能被调用。
 */

namespace
{
    using namespace ID::Bench::Production;

    // ---- 0 插件：合法纯内核产品 ----
    using ZeroShape                  = ShapeOf<std::tuple<>, 0, 0, 1, 1>;
    inline constexpr auto kZeroPlan = ID::PluginManager::sort_and_validate(
        with_evidence(make_input<ZeroShape>(std::tuple<>{}, std::array<PluginIdentity, 0>{})));

    static_assert(kZeroPlan.diagnostic.code == PluginError::None, "positive_basics: 0 active 必须合法");
    static_assert(kZeroPlan.active_count == 0, "positive_basics: 0 active 的 active_count 必须为 0");
    static_assert(kZeroPlan.edge_count == 0 && kZeroPlan.provision_count == 0,
                  "positive_basics: 0 active 不得产生边或能力记录");

    // ---- 1 插件 ----
    inline constexpr auto kOneCatalog  = std::tuple{ entry_leaf(alpha()) };
    inline constexpr auto kOneSelected = std::array<PluginIdentity, 1>{ alpha() };
    using OneShape                     = ShapeOf<Plain<decltype(kOneCatalog)>, 1, 1, 1, 1>;
    inline constexpr auto kOnePlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<OneShape>(kOneCatalog, kOneSelected)));

    static_assert(kOnePlan.diagnostic.code == PluginError::None, "positive_basics: 1 active 必须合法");
    static_assert(kOnePlan.active_count == 1 && kOnePlan.provision_count == 1,
                  "positive_basics: 1 active 必须有一条能力记录");

    // ---- canonical 顺序 + 名字 tie-break：乱序输入必须按名字字节序编号 ----
    inline constexpr auto kCatalog = std::tuple{ entry_leaf(gamma()), entry_leaf(alpha()), entry_leaf(beta()) };
    inline constexpr auto kSelected = std::array<PluginIdentity, 3>{ gamma(), beta(), alpha() };
    using Shape                     = ShapeOf<Plain<decltype(kCatalog)>, 3, 3, 1, 1>;
    inline constexpr auto kPlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<Shape>(kCatalog, kSelected)));

    static_assert(kPlan.diagnostic.code == PluginError::None, "positive_basics: 三个独立插件必须合法");
    static_assert(kPlan.active_identity[0].view() == "ID.Case.Alpha", "positive_basics: canonical[0] 必须是 Alpha");
    static_assert(kPlan.active_identity[1].view() == "ID.Case.Beta", "positive_basics: canonical[1] 必须是 Beta");
    static_assert(kPlan.active_identity[2].view() == "ID.Case.Gamma", "positive_basics: canonical[2] 必须是 Gamma");
    static_assert(kPlan.topo_order[0] == 0 && kPlan.topo_order[1] == 1 && kPlan.topo_order[2] == 2,
                  "positive_basics: 全部就绪时拓扑序按名字字节序");

    // ---- 依赖边让上游先出队；名字小的下游不能插到上游前面 ----
    inline constexpr auto kTieCatalog  = std::tuple{ entry_dep(alpha(), kNameBeta), entry_leaf(beta()) };
    inline constexpr auto kTieSelected = std::array<PluginIdentity, 2>{ alpha(), beta() };
    using TieShape                     = ShapeOf<Plain<decltype(kTieCatalog)>, 2, 2, 1, 1>;
    inline constexpr auto kTiePlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<TieShape>(kTieCatalog, kTieSelected)));

    static_assert(kTiePlan.diagnostic.code == PluginError::None, "positive_basics: 依赖图必须合法");
    static_assert(kTiePlan.topo_order[0] == kTiePlan.find_active("ID.Case.Beta"),
                  "positive_basics: 上游 Beta 必须先于下游 Alpha");
    static_assert(kTiePlan.topo_order[1] == kTiePlan.find_active("ID.Case.Alpha"),
                  "positive_basics: 下游 Alpha 必须排在 Beta 之后");
} // namespace

namespace ID::Bench::Production::Positive
{
    ProbeResult basics() noexcept
    {
        using namespace ID::Bench::Production;
        return ProbeResult{ kPlan.active_count, kPlan.edge_count, kPlan.provision_count,
                            static_cast<uint8_t>(kPlan.diagnostic.code) };
    }
} // namespace ID::Bench::Production::Positive
