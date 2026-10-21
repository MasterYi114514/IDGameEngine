#include "ProductionStress.hpp"

/**
 *  默认限制档：128 active / 256 catalog / 32 需求 / 每组 16 候选
 *  **预期在 GCC 16.2.0 默认 -fconstexpr-ops-limit=33554432 下失败**；
 *  失败原因必须记录为"常量求值操作数超限"，不是用例自身写错。
 *  该目标的存在就是为了不把默认限制下的失败抹掉（计划书 6.5 / 15）。
 */

namespace
{
    using namespace ID::Bench::Production;
    using namespace ID::Bench::Production::Stress;

    inline constexpr StressProfile kProfile{ 128, 256, 32, 16 };
    inline constexpr auto          kCatalog  = make_catalog<kProfile>(std::make_index_sequence<kProfile.catalog>{});
    inline constexpr auto          kSelected = make_selected<kProfile>(std::make_index_sequence<kProfile.active>{});

    using Shape = AssemblyShape<Plain<decltype(kCatalog)>, 128, 128, 4096, 128, 64>;
    inline constexpr auto kInput = make_input_with_evidence<kProfile, Shape>(kCatalog, kSelected);
    static_assert(evidence_complete<kProfile, Shape>(kInput), "scale_limit_128x32x16: 证据记录条数必须完整");

    inline constexpr auto kPlan = ID::PluginManager::sort_and_validate(kInput);

    static_assert(kPlan.diagnostic.code == PluginError::None, "scale_limit_128x32x16: 若通过必须合法");
    static_assert(kPlan.active_count == 128, "scale_limit_128x32x16: active 必须为 128");
} // namespace
