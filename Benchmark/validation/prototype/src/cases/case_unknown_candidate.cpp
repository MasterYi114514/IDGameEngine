#include "CaseCommon.hpp"

/**
 *  未知候选：候选名必须出现在完整 catalog 中，不要求它一定被选中（计划书 5.3）
 *  合法对照：候选换成 catalog 内已声明的插件。
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
        constexpr std::array<PluginIdentity, 2> selected{ alpha(), gamma() };
        return sort_and_validate(std::tuple{ declaration_alpha, declaration_beta, declaration_gamma }, selected);
    }
#else
    consteval auto make_case_plan() noexcept
    {
        constexpr auto declaration_alpha = declare_dependencies(alpha(), std::tuple<>{});
        constexpr auto declaration_gamma = declare_dependencies(gamma(), std::tuple{ pair_of(kNameAlpha, kNameGhost) });
        constexpr std::array<PluginIdentity, 2> selected{ alpha(), gamma() };
        return sort_and_validate(std::tuple{ declaration_alpha, declaration_gamma }, selected);
    }
#endif

    constexpr auto kPlan = make_case_plan();

#if defined(FIXTURE_CASE_LEGAL)
    static_assert(kPlan.error == ErrorCode::None, "unknown_candidate: 合法对照必须组装成功");
#else
    ID_FIXTURE_EXPECT_ERROR(kPlan, UnknownCandidate);
#endif
} // namespace
