#include "CaseCommon.hpp"

/**
 *  缺失提供者：某需求在选中集合内 0 个候选时必须拒绝，且不自动启用（计划书 6.2）
 *  合法对照：同一 catalog，把 A 也选中。
 */

namespace
{
    using namespace ID::Bench::Fixture;
    using namespace ID::Bench::Fixture::Cases;

#if defined(FIXTURE_CASE_LEGAL)
    consteval auto make_case_plan() noexcept
    {
        constexpr auto declaration_alpha = declare_dependencies(alpha(), std::tuple<>{});
        constexpr auto declaration_gamma = declare_dependencies(gamma(), std::tuple{ group_of(kNameAlpha) });
        constexpr std::array<PluginIdentity, 2> selected{ alpha(), gamma() };
        return sort_and_validate(std::tuple{ declaration_alpha, declaration_gamma }, selected);
    }
#else
    consteval auto make_case_plan() noexcept
    {
        constexpr auto declaration_alpha = declare_dependencies(alpha(), std::tuple<>{});
        constexpr auto declaration_gamma = declare_dependencies(gamma(), std::tuple{ group_of(kNameAlpha) });
        constexpr std::array<PluginIdentity, 1> selected{ gamma() };    // 只选中下游，上游未启用
        return sort_and_validate(std::tuple{ declaration_alpha, declaration_gamma }, selected);
    }
#endif

    constexpr auto kPlan = make_case_plan();

#if defined(FIXTURE_CASE_LEGAL)
    static_assert(kPlan.error == ErrorCode::None, "missing_provider: 合法对照必须组装成功");
#else
    ID_FIXTURE_EXPECT_ERROR(kPlan, MissingProvider);
#endif
} // namespace
