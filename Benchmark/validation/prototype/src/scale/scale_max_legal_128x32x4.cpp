#include "FixtureStressSupport.hpp"

/**
 *  默认常量求值限制下实测通过的最大组合：128 active / 256 catalog / 32 需求 / 每组 4 候选
 *  （14,272 个候选叶子；每组 8 候选与 16 候选在默认 -fconstexpr-ops-limit=33554432 下超限，
 *    见 validation/baseline/raw/scale-probe-*.log 与 prototype-p0-evidence.txt 的成本测量）。
 *  本文件是"可重复目标"：它必须通过，用来防止后续实现悄悄退化。
 */

namespace
{
    using namespace ID::Bench::Fixture;
    using namespace ID::Bench::Fixture::Stress;

    inline constexpr StressProfile kProfile{ 128, 256, 32, 4 };

    constexpr auto kCatalog  = make_catalog<kProfile>(std::make_index_sequence<kProfile.catalog>{});
    constexpr auto kSelected = make_selected<kProfile>();
    constexpr auto kPlan     = sort_and_validate(kCatalog, kSelected);

    // 逐需求绑定总数 = sum(min(i,32), i=0..127) = 496 + 3072 = 3568：
    // 该数字证明每个 active 的每个需求都真的完成了绑定，而不是被截断的求值意外通过
    inline constexpr size_t kExpectedEdges = (31 * 32) / 2 + 96 * 32;

    static_assert(kPlan.error == ErrorCode::None, "scale_max_legal: 该组合必须在默认限制下合法");
    static_assert(kPlan.active_count == 128, "scale_max_legal: active 数必须为 128");
    static_assert(kPlan.edge_count == kExpectedEdges, "scale_max_legal: 去重边数必须等于逐需求绑定总数");
    static_assert(kPlan.topo_order[0] == find_active_index(kPlan, kSelected[0].view()),
                  "scale_max_legal: 拓扑序首个必须是无上游的 P0");
    static_assert(kPlan.fingerprint != 0, "scale_max_legal: 必须输出确定性指纹");
} // namespace
