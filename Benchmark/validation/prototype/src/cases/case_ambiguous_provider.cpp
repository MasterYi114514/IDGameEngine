#include "CaseCommon.hpp"

/**
 *  歧义提供者：同一需求在选中集合内命中 >=2 个候选时必须拒绝，不挑第一个、不挑最新（计划书 6.2）
 *  合法对照：两个候选都在 catalog，但只有 A 被选中。
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
        constexpr auto declaration_gamma = declare_dependencies(gamma(), std::tuple{ pair_of(kNameAlpha, kNameBeta) });
        constexpr std::array<PluginIdentity, 2> selected{ gamma(), alpha() };    // 只选中 A，不选 B
        return sort_and_validate(std::tuple{ declaration_alpha, declaration_beta, declaration_gamma }, selected);
    }
#else
    consteval auto make_case_plan() noexcept
    {
        constexpr auto declaration_alpha = declare_dependencies(alpha(), std::tuple<>{});
        constexpr auto declaration_beta  = declare_dependencies(beta(), std::tuple<>{});
        constexpr auto declaration_gamma = declare_dependencies(gamma(), std::tuple{ pair_of(kNameAlpha, kNameBeta) });
        constexpr std::array<PluginIdentity, 3> selected{ alpha(), beta(), gamma() };    // 两个候选都被选中
        return sort_and_validate(std::tuple{ declaration_alpha, declaration_beta, declaration_gamma }, selected);
    }
#endif

    constexpr auto kPlan = make_case_plan();

#if defined(FIXTURE_CASE_LEGAL)
    static_assert(kPlan.error == ErrorCode::None, "ambiguous_provider: 合法对照必须组装成功");
#else
    ID_FIXTURE_EXPECT_ERROR(kPlan, AmbiguousProvider);
#endif
} // namespace
