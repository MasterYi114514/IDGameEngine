#include "CaseCommon.hpp"

/**
 *  重复 active：选中集合里同一个插件出现两次必须拒绝（计划书 7 产品选择）
 *  合法对照：两个不同的插件各选中一次。
 */

namespace
{
    using namespace ID::Bench::Fixture;
    using namespace ID::Bench::Fixture::Cases;

#if defined(FIXTURE_CASE_LEGAL)
    consteval auto make_case_plan() noexcept
    {
        constexpr auto declaration_alpha = declare_dependencies(alpha(), std::tuple<>{});
        constexpr auto declaration_beta  = declare_dependencies(beta(), std::tuple<>{});
        constexpr std::array<PluginIdentity, 2> selected{ alpha(), beta() };
        return sort_and_validate(std::tuple{ declaration_alpha, declaration_beta }, selected);
    }
#else
    consteval auto make_case_plan() noexcept
    {
        constexpr auto declaration_alpha = declare_dependencies(alpha(), std::tuple<>{});
        constexpr std::array<PluginIdentity, 2> selected{ alpha(), alpha() };
        return sort_and_validate(std::tuple{ declaration_alpha }, selected);
    }
#endif

    constexpr auto kPlan = make_case_plan();

#if defined(FIXTURE_CASE_LEGAL)
    static_assert(kPlan.error == ErrorCode::None, "duplicate_active: 合法对照必须组装成功");
#else
    ID_FIXTURE_EXPECT_ERROR(kPlan, DuplicateActive);
#endif
} // namespace
