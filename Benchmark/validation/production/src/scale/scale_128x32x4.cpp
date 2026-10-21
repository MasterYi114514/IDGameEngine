#include "ProductionStress.hpp"

/**
 *  规模档：128 active / 256 catalog / 32 需求 / 每组 4 候选（3 个未选备选同 Cap0）
 *  完整 contracts/revisions/provides 元数据；默认常量求值预算档。
 */

namespace
{
    using namespace ID::Bench::Production;
    using namespace ID::Bench::Production::Stress;

    inline constexpr StressProfile kProfile{ 128, 256, 32, 4 };
    inline constexpr auto          kCatalog  = make_catalog<kProfile>(std::make_index_sequence<kProfile.catalog>{});
    inline constexpr auto          kSelected = make_selected<kProfile>(std::make_index_sequence<kProfile.active>{});

    using Shape = AssemblyShape<Plain<decltype(kCatalog)>, 128, 128, 4096, 128, 64>;
    inline constexpr auto kPlan =
        ID::PluginManager::sort_and_validate(make_input_with_evidence<kProfile, Shape>(kCatalog, kSelected));

    static_assert(kPlan.diagnostic.code == PluginError::None, "scale_128x32x4: 必须合法");
    static_assert(kPlan.active_count == 128, "scale_128x32x4: active 必须为 128");
    static_assert(kPlan.edge_count == 127, "scale_128x32x4: 去重边只能有 127 条");
    static_assert(kPlan.provider[127 * 32 + 31] == kPlan.find_active("ID.Case.N0"),
                  "scale_128x32x4: 最后一个 active 的最后一组必须绑定 P0");
    static_assert(kPlan.fingerprint != 0, "scale_128x32x4: 必须输出确定性指纹");
} // namespace
