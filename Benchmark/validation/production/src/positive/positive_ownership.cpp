#include "ProductionCaseSupport.hpp"
#include "ProductionPositiveProbes.hpp"

/**
 *  正向对照：单插件 >=5 条 provides / >=2 个槽位；多条无槽位能力的修订记录；
 *  module + lifecycle=none + 槽位在 P1 阶段合法（是否发布由 P3 runtime 决定）
 *  编译期断言 + 运行期探针：TU 必须进入 ProductionCaseControls 的链接行并能被调用。
 */

namespace
{
    using namespace ID::Bench::Production;

    // ---- 5 条 provides、2 个槽位 ----
    inline constexpr auto kMultiCatalog = std::tuple{
        make_entry<5>(alpha(), std::tuple<>{}, std::array<std::string_view, 0>{}, kTargetAlpha, kNamespaceAlpha,
                      provides5(kCapAlpha, 0)) };
    inline constexpr auto kMultiSelected = std::array<PluginIdentity, 1>{ alpha() };
    using MultiShape                     = ShapeOf<Plain<decltype(kMultiCatalog)>, 1, 1, 1, 1>;
    inline constexpr auto kMultiPlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<MultiShape>(kMultiCatalog, kMultiSelected)));

    static_assert(kMultiPlan.diagnostic.code == PluginError::None, "positive_ownership: 5 provides/2 slots 必须合法");
    static_assert(kMultiPlan.provision_count == 5, "positive_ownership: 五条 provides 必须全部进入扁平表");
    static_assert(kMultiPlan.active_provision_count[0] == 5, "positive_ownership: 每插件 provides 计数必须为 5");
    static_assert(kMultiPlan.provision_slot[0] == 0 && kMultiPlan.provision_slot[1] == 1,
                  "positive_ownership: canonical 前两条能力必须占槽 0/1");
    static_assert(kMultiPlan.provision_slot[2] == kNoSlot && kMultiPlan.provision_slot[4] == kNoSlot,
                  "positive_ownership: 后三条能力必须是无槽位记录");
    static_assert(kMultiPlan.provision_api_revision[0] == 1 && kMultiPlan.provision_api_revision[1] == 2 &&
                      kMultiPlan.provision_api_revision[4] == 5,
                  "positive_ownership: 每条能力必须保留自己的接口修订");
    static_assert(kMultiPlan.slot_owner[0] == 0 && kMultiPlan.slot_owner[1] == 0,
                  "positive_ownership: 两个槽位必须都归属同一插件");
    static_assert(kMultiPlan.slot_owner[2] == kNoIndex, "positive_ownership: 未占用槽必须是 kNoIndex");

    // ---- 多条无槽位能力的修订记录 ----
    inline constexpr auto kNoSlotCatalog = std::tuple{
        make_entry<2>(alpha(), std::tuple<>{}, std::array<std::string_view, 0>{}, kTargetAlpha, kNamespaceAlpha,
                      std::array<Provision, 2>{ { provision(kCapBeta, 7, kNoSlot), provision(kCapAlpha, 3, kNoSlot) } }) };
    inline constexpr auto kNoSlotSelected = std::array<PluginIdentity, 1>{ alpha() };
    using NoSlotShape                     = ShapeOf<Plain<decltype(kNoSlotCatalog)>, 1, 1, 1, 1>;
    inline constexpr auto kNoSlotPlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<NoSlotShape>(kNoSlotCatalog, kNoSlotSelected)));

    static_assert(kNoSlotPlan.diagnostic.code == PluginError::None, "positive_ownership: 多条无槽位能力必须合法");
    static_assert(kNoSlotPlan.provision_count == 2, "positive_ownership: 两条无槽位记录必须保留");
    static_assert(kNoSlotPlan.provision_capability_view(0) == "ID.Case.CapA" &&
                      kNoSlotPlan.provision_api_revision[0] == 3,
                  "positive_ownership: canonical 首条必须是 CapA(rev 3)");
    static_assert(kNoSlotPlan.provision_capability_view(1) == "ID.Case.CapB" &&
                      kNoSlotPlan.provision_api_revision[1] == 7,
                  "positive_ownership: canonical 次条必须是 CapB(rev 7)");
    static_assert(kNoSlotPlan.provision_slot[0] == kNoSlot && kNoSlotPlan.provision_slot[1] == kNoSlot,
                  "positive_ownership: 无槽位能力必须记录 kNoSlot");

    // ---- module + lifecycle=none + 槽位：P1 阶段合法 ----
    inline constexpr auto kNoneCatalog = std::tuple{
        make_entry(alpha(), std::tuple<>{}, std::array<std::string_view, 0>{}, kTargetAlpha, kNamespaceAlpha,
                   provides1(kCapAlpha, 1, 0), PluginKind::Module, PluginLifecycle::None) };
    inline constexpr auto kNoneSelected = std::array<PluginIdentity, 1>{ alpha() };
    using NoneShape                     = ShapeOf<Plain<decltype(kNoneCatalog)>, 1, 1, 1, 1>;
    inline constexpr auto kNonePlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<NoneShape>(kNoneCatalog, kNoneSelected)));

    static_assert(kNonePlan.diagnostic.code == PluginError::None,
                  "positive_ownership: module+none+slot 在 P1 阶段必须合法");
    static_assert(kNonePlan.provision_slot[0] == 0 && kNonePlan.slot_owner[0] == 0,
                  "positive_ownership: 槽位归属必须记录，运行期是否发布留给 P3");
} // namespace

namespace ID::Bench::Production::Positive
{
    ProbeResult ownership() noexcept
    {
        using namespace ID::Bench::Production;
        return ProbeResult{ kMultiPlan.active_count, kMultiPlan.edge_count, kMultiPlan.provision_count,
                            static_cast<uint8_t>(kMultiPlan.diagnostic.code) };
    }
} // namespace ID::Bench::Production::Positive
