#include "ProductionStress.hpp"

/**
 *  默认预算边界档：128 active / 256 catalog / 32 需求 / 每组 1 候选
 *  用于确认"生产 API 在默认 33554432 预算下到底能通过到哪一档"。
 */

namespace
{
    using namespace ID::Bench::Production;
    using namespace ID::Bench::Production::Stress;

    inline constexpr StressProfile kProfile{ 128, 256, 32, 1 };
    inline constexpr auto          kCatalog  = make_catalog<kProfile>(std::make_index_sequence<kProfile.catalog>{});
    inline constexpr auto          kSelected = make_selected<kProfile>(std::make_index_sequence<kProfile.active>{});

    using Shape = AssemblyShape<Plain<decltype(kCatalog)>, 128, 128, 4096, 128, 64>;
    inline constexpr auto kInput = make_input_with_evidence<kProfile, Shape>(kCatalog, kSelected);
    static_assert(evidence_complete<kProfile, Shape>(kInput), "scale_128x32x1: 证据记录条数必须完整");

    inline constexpr auto kPlan = ID::PluginManager::sort_and_validate(kInput);

    static_assert(kPlan.diagnostic.code == PluginError::None, "scale_128x32x1: 必须合法");
    static_assert(kPlan.active_count == 128 && kPlan.edge_count == 127,
                  "scale_128x32x1: active=128、去重边=127");
    static_assert(kPlan.active_requirement_count[127] == 32, "scale_128x32x1: 最后一个 active 有 32 条需求");
} // namespace
