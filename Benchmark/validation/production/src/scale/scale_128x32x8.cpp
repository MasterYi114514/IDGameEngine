#include "ProductionStress.hpp"

/**
 *  规模档：128 active / 256 catalog / 32 需求 / 每组 8 候选（7 个未选备选同 Cap0）
 *  实测（GCC 16.2.0 默认 -fconstexpr-ops-limit=33554432）：夹具的三个常量表达式
 *  （kCatalog / kSelected / kInput）各自通过，失败发生在 sort_and_validate 的求值里
 *  （PluginCompileTime.hpp 的候选视图/名字比较处）；134217728 下本档通过。
 *  因此本档记录的是"默认预算装不下 8 候选的完整校验"，不是夹具构造超限。
 */

namespace
{
    using namespace ID::Bench::Production;
    using namespace ID::Bench::Production::Stress;

    inline constexpr StressProfile kProfile{ 128, 256, 32, 8 };
    inline constexpr auto          kCatalog  = make_catalog<kProfile>(std::make_index_sequence<kProfile.catalog>{});
    inline constexpr auto          kSelected = make_selected<kProfile>(std::make_index_sequence<kProfile.active>{});

    using Shape = AssemblyShape<Plain<decltype(kCatalog)>, 128, 128, 4096, 128, 64>;
    inline constexpr auto kInput = make_input_with_evidence<kProfile, Shape>(kCatalog, kSelected);
    static_assert(evidence_complete<kProfile, Shape>(kInput), "scale_128x32x8: 证据记录条数必须完整");

    inline constexpr auto kPlan = ID::PluginManager::sort_and_validate(kInput);

    static_assert(kPlan.diagnostic.code == PluginError::None, "scale_128x32x8: 必须合法");
    static_assert(kPlan.active_count == 128, "scale_128x32x8: active 必须为 128");
    static_assert(kPlan.edge_count == 127, "scale_128x32x8: 去重边只能有 127 条");
    static_assert(kPlan.provision_count == 128, "scale_128x32x8: 每条目一条 provides");
} // namespace
