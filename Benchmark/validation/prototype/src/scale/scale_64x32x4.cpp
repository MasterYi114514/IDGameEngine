#include "FixtureStressSupport.hpp"

/**
 *  规模阶梯 3：64 active / catalog 128 / 每插件最多 32 个需求组 / 每组 4 个候选
 *  每组只有 1 个候选被选中，其余候选只在 catalog 中存在 —— 这是"候选放大但不产生歧义"的合法形态。
 */

namespace
{
    using namespace ID::Bench::Fixture;
    using namespace ID::Bench::Fixture::Stress;

    inline constexpr StressProfile kProfile{ 64, 128, 32, 4 };

    constexpr auto kCatalog  = make_catalog<kProfile>(std::make_index_sequence<kProfile.catalog>{});
    constexpr auto kSelected = make_selected<kProfile>();
    constexpr auto kPlan     = sort_and_validate(kCatalog, kSelected);

    static_assert(kPlan.error == ErrorCode::None, "scale_64x32x4: 必须合法");
    static_assert(kPlan.active_count == 64, "scale_64x32x4: active 数必须为 64");
    static_assert(kPlan.edge_count > 0, "scale_64x32x4: 必须有真实依赖边");
} // namespace
