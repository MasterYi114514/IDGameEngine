#include "FixtureStressSupport.hpp"

/**
 *  压力目标（**编译期预算有限且按编译器限定**）：128 active / catalog 256 / 32 需求 / 每组 16 候选
 *  1. 本 TU 只在 CMake 的 GNU 分支里创建目标，并显式追加
 *     -fconstexpr-ops-limit=134217728（见 validation/prototype/CMakeLists.txt 第 9 节）；
 *  2. 依据：同一组合在 GCC 16.2.0 默认 33554432 下超限失败，67108864 仍失败，134217728 实测通过
 *     （完整命令与退出码见 validation/baseline/raw/scale-required-limit.log）；
 *  3. 默认限制下的失败由独立目标 fixture_scale_limit_128x32x16 记录，本目标不掩盖该事实；
 *  4. MSVC / clang 未实测：不创建目标、不宣称通过。
 */

#if !defined(__GNUC__) || defined(__clang__)
#error "scale_target_128x32x16: 有限常量求值预算只在 GCC 16.2.0 实测；其它编译器不得据此宣称通过"
#endif

namespace
{
    using namespace ID::Bench::Fixture;
    using namespace ID::Bench::Fixture::Stress;

    inline constexpr StressProfile kProfile{ 128, 256, 32, 16 };

    constexpr auto kCatalog  = make_catalog<kProfile>(std::make_index_sequence<kProfile.catalog>{});
    constexpr auto kSelected = make_selected<kProfile>();
    constexpr auto kPlan     = sort_and_validate(kCatalog, kSelected);

    // 期望的去重边数：第 i 个 active 有 min(i, 32) 个需求组，各组把不同的更小下标当上游，
    // 所以 sum(min(i,32), i=0..127) = 0+1+...+31 + 96*32 = 3568；这个数字同时证明
    // "每个 active 的每个需求都真的完成了绑定"，不是被截断的求值意外通过。
    inline constexpr size_t kExpectedEdges = (31 * 32) / 2 + 96 * 32;

    static_assert(kPlan.error == ErrorCode::None, "scale_target_128x32x16: 该组合必须在有限预算下合法");
    static_assert(kPlan.active_count == 128, "scale_target_128x32x16: active 数必须为 128");
    static_assert(kPlan.edge_count == kExpectedEdges, "scale_target_128x32x16: 去重边数必须等于逐需求绑定总数");
    static_assert(kPlan.provider[static_cast<size_t>(find_active_index(kPlan, kSelected[127].view())) * kMaxRequirements] ==
                      find_active_index(kPlan, kSelected[126].view()),
                  "scale_target_128x32x16: 最后一个 active 的首个需求必须绑定到上一个插件");
    static_assert(kPlan.topo_order[0] == find_active_index(kPlan, kSelected[0].view()),
                  "scale_target_128x32x16: 拓扑序首个必须是无上游的 P0");
    static_assert(kPlan.fingerprint != 0, "scale_target_128x32x16: 必须输出确定性指纹");
} // namespace
