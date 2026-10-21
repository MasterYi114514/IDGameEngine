#include "ProductionCaseSupport.hpp"

/**
 *  规模档：128 active / 129 catalog / 无依赖 / 每组 1 候选
 *  证明空需求、无槽位能力在 active 上限下仍合法，且证据逐项匹配。
 */

namespace
{
    using namespace ID::Bench::Production;

    inline constexpr auto kCatalog = generated_leaf_tuple<129>();

    template <size_t... Index>
    consteval auto make_selected(std::index_sequence<Index...>) noexcept
    {
        return std::array<PluginIdentity, sizeof...(Index)>{ generated_identity(Index)... };
    }

    inline constexpr auto kSelected = make_selected(std::make_index_sequence<128>{});
    using Shape                     = ShapeOf<Plain<decltype(kCatalog)>, 128, 128, 1, 1>;
    inline constexpr auto kPlan =
        ID::PluginManager::sort_and_validate(with_generated_leaf_evidence(make_input<Shape>(kCatalog, kSelected)));

    static_assert(kPlan.diagnostic.code == PluginError::None, "scale_active_128: 必须合法");
    static_assert(kPlan.active_count == 128 && kPlan.provision_count == 128,
                  "scale_active_128: active 与 provision 计数必须为 128");
    static_assert(kPlan.edge_count == 0, "scale_active_128: 无依赖不得产生边");
} // namespace
