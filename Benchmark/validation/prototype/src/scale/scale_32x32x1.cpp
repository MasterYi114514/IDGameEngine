#include "FixtureStressSupport.hpp"

/**
 *  规模阶梯 2：32 active / catalog 64 / 每插件最多 32 个需求组 / 每组 1 个候选
 *  @note 依赖只指向更小下标，因此合法且无环；候选数 1 时不使用 catalog 里的备用名字。
 */

namespace
{
    using namespace ID::Bench::Fixture;
    using namespace ID::Bench::Fixture::Stress;

    inline constexpr StressProfile kProfile{ 32, 64, 32, 1 };

    constexpr auto kCatalog  = make_catalog<kProfile>(std::make_index_sequence<kProfile.catalog>{});
    constexpr auto kSelected = make_selected<kProfile>();
    constexpr auto kPlan     = sort_and_validate(kCatalog, kSelected);

    static_assert(kPlan.error == ErrorCode::None, "scale_32x32x1: 必须合法");
    static_assert(kPlan.active_count == 32, "scale_32x32x1: active 数必须为 32");
    static_assert(kPlan.edge_count > 0, "scale_32x32x1: 必须有真实依赖边");
} // namespace
