#include "ProductionStress.hpp"

/**
 *  默认预算边界档：128 active / 256 catalog / 32 需求 / 每组 2 候选（1 个未选备选同 Cap0）
 *  用于确认生产 API 的默认预算边界是否在 1 与 4 候选之间。
 */

namespace
{
    using namespace ID::Bench::Production;
    using namespace ID::Bench::Production::Stress;

    inline constexpr StressProfile kProfile{ 128, 256, 32, 2 };
    inline constexpr auto          kCatalog  = make_catalog<kProfile>(std::make_index_sequence<kProfile.catalog>{});
    inline constexpr auto          kSelected = make_selected<kProfile>(std::make_index_sequence<kProfile.active>{});

    using Shape = AssemblyShape<Plain<decltype(kCatalog)>, 128, 128, 4096, 128, 64>;
    inline constexpr auto kPlan =
        ID::PluginManager::sort_and_validate(make_input_with_evidence<kProfile, Shape>(kCatalog, kSelected));

    static_assert(kPlan.diagnostic.code == PluginError::None, "scale_128x32x2: 必须合法");
    static_assert(kPlan.active_count == 128 && kPlan.edge_count == 127,
                  "scale_128x32x2: active=128、去重边=127");
} // namespace
