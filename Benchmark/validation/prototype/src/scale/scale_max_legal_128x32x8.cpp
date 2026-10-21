#include "FixtureStressSupport.hpp"

/**
 *  默认常量求值限制下实测通过的最大组合（本轮优化后）：128 active / 256 catalog / 32 需求 / 每组 8 候选
 *  （28,544 个候选叶子）。
 *  1. 优化前该档在默认 -fconstexpr-ops-limit=33554432 下超限失败，本轮把
 *     "声明单趟化 + 紧凑候选键 + 按实际需求数建图/指纹" 做完后已能真实通过；
 *  2. 本文件是可重复目标：它必须通过，用来防止后续实现悄悄退化回 32×4；
 *  3. 每组 16 候选（57,088 个叶子）在默认限制下仍失败，见 scale_limit_128x32x16.cpp。
 */

namespace
{
    using namespace ID::Bench::Fixture;
    using namespace ID::Bench::Fixture::Stress;

    inline constexpr StressProfile kProfile{ 128, 256, 32, 8 };

    // 逐需求绑定总数 = sum(min(i,32), i=0..127) = 496 + 3072 = 3568
    inline constexpr size_t kExpectedEdges = (31 * 32) / 2 + 96 * 32;

    constexpr auto kCatalog  = make_catalog<kProfile>(std::make_index_sequence<kProfile.catalog>{});
    constexpr auto kSelected = make_selected<kProfile>();
    constexpr auto kPlan     = sort_and_validate(kCatalog, kSelected);

    static_assert(kPlan.error == ErrorCode::None, "scale_max_legal_32x8: 该组合必须在默认限制下合法");
    static_assert(kPlan.active_count == 128, "scale_max_legal_32x8: active 数必须为 128");
    static_assert(kPlan.edge_count == kExpectedEdges, "scale_max_legal_32x8: 去重边数必须等于逐需求绑定总数");
    static_assert(kPlan.topo_order[0] == find_active_index(kPlan, kSelected[0].view()),
                  "scale_max_legal_32x8: 拓扑序首个必须是无上游的 P0");
    static_assert(kPlan.fingerprint != 0, "scale_max_legal_32x8: 必须输出确定性指纹");
} // namespace
