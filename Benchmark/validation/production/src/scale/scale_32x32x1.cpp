#include "ProductionStress.hpp"

/**
 *  规模档：32 active / 64 catalog / 32 需求 / 每组 1 候选
 *  浅图（全部依赖 P0），但需求组数与 active 数真实；用于与 128 active 档对照。
 */

namespace
{
    using namespace ID::Bench::Production;
    using namespace ID::Bench::Production::Stress;

    inline constexpr StressProfile kProfile{ 32, 64, 32, 1 };
    inline constexpr auto          kCatalog  = make_catalog<kProfile>(std::make_index_sequence<kProfile.catalog>{});
    inline constexpr auto          kSelected = make_selected<kProfile>(std::make_index_sequence<kProfile.active>{});

    using Shape = AssemblyShape<Plain<decltype(kCatalog)>, 32, 32, 1024, 64, 64>;
    inline constexpr auto kInput = make_input_with_evidence<kProfile, Shape>(kCatalog, kSelected);
    static_assert(evidence_complete<kProfile, Shape>(kInput), "scale_32x32x1: 证据记录条数必须完整");

    inline constexpr auto kPlan = ID::PluginManager::sort_and_validate(kInput);

    static_assert(kPlan.diagnostic.code == PluginError::None, "scale_32x32x1: 必须合法");
    static_assert(kPlan.active_count == 32, "scale_32x32x1: active 必须为 32");
    static_assert(kPlan.edge_count == 31, "scale_32x32x1: 31 个下游各有一条去重边");
    static_assert(kPlan.active_requirement_count[31] == 32, "scale_32x32x1: 最后一个 active 必须有 32 条需求");
    static_assert(kPlan.provider[31 * 32] == kPlan.find_active("ID.Case.N0"),
                  "scale_32x32x1: 最后一个 active 的首个需求必须绑定 P0");
} // namespace
