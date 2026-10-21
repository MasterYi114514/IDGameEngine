#include "CaseCommon.hpp"

#include "FixtureStressSupport.hpp"

/**
 *  选中数量上限：128 合法，129 必须给容量诊断（计划书 6.5 / 16.1）
 *  合法对照：同一份压力输入改为 128 个插件。
 */

namespace
{
    using namespace ID::Bench::Fixture;
    using namespace ID::Bench::Fixture::Stress;

#if defined(FIXTURE_CASE_LEGAL)
    inline constexpr StressProfile kProfile{ 128, 128, 0, 1 };
#else
    inline constexpr StressProfile kProfile{ 129, 129, 0, 1 };
#endif

    constexpr auto kCatalog  = make_catalog<kProfile>(std::make_index_sequence<kProfile.catalog>{});
    constexpr auto kSelected = make_selected<kProfile>();
    constexpr auto kPlan     = sort_and_validate(kCatalog, kSelected);

#if defined(FIXTURE_CASE_LEGAL)
    static_assert(kPlan.error == ErrorCode::None, "active_overflow: 128 个插件必须合法");
    static_assert(kPlan.active_count == 128, "active_overflow: 合法对照必须输出 128 个 active");
#else
    ID_FIXTURE_EXPECT_ERROR(kPlan, TooManyActive);
#endif
} // namespace
