#include "FixtureStressSupport.hpp"

/**
 *  默认限制的上限档（预期编译失败，不作为通过项）：
 *  128 active / catalog 256 / 32 需求 / 每组 16 个候选（57,088 个候选叶子）
 *  1. 本档记录的事实是"GCC 16.2.0 默认 -fconstexpr-ops-limit=33554432 下满组合仍超限"；
 *  2. 同一个 TU 在有限预算目标 fixture_scale_target_128x32x16 上换用
 *     -fconstexpr-ops-limit=134217728 后真实通过（见 src/scale/scale_target_128x32x16.cpp）；
 *  3. 真实通过必须有具体的绑定数与指纹断言，不能用"编译过了"代替语义正确性。
 */

namespace
{
    using namespace ID::Bench::Fixture;
    using namespace ID::Bench::Fixture::Stress;

    inline constexpr StressProfile kProfile{ 128, 256, 32, 16 };

    constexpr auto kCatalog  = make_catalog<kProfile>(std::make_index_sequence<kProfile.catalog>{});
    constexpr auto kSelected = make_selected<kProfile>();
    constexpr auto kPlan     = sort_and_validate(kCatalog, kSelected);

    static_assert(kPlan.error == ErrorCode::None, "scale_limit_128x32x16: 期望因操作数上限而不是语义错误失败");
    static_assert(kPlan.active_count == 128, "scale_limit_128x32x16: active 数必须为 128");
} // namespace
