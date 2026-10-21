#include "FixtureStressSupport.hpp"

/**
 *  规模阶梯 1：128 个 active、0 需求 —— 只验证 active 上限本身
 *  @note 每个阶梯文件单独成一个 TU，便于逐档测量编译期成本（不合并成一个巨大 TU）。
 */

namespace
{
    using namespace ID::Bench::Fixture;
    using namespace ID::Bench::Fixture::Stress;

    inline constexpr StressProfile kProfile{ 128, 128, 0, 1 };

    constexpr auto kCatalog  = make_catalog<kProfile>(std::make_index_sequence<kProfile.catalog>{});
    constexpr auto kSelected = make_selected<kProfile>();
    constexpr auto kPlan     = sort_and_validate(kCatalog, kSelected);

    static_assert(kPlan.error == ErrorCode::None, "scale_active_128: 128 个 active 必须合法");
    static_assert(kPlan.active_count == 128, "scale_active_128: active 数必须为 128");
    static_assert(kPlan.edge_count == 0, "scale_active_128: 无依赖时不得有边");
} // namespace
