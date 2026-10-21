#include "CaseCommon.hpp"

/**
 *  组内重复候选：同一需求组里重复出现同一个名字必须拒绝（计划书 5.3）
 *  合法对照：同一组候选换成两个不同插件。
 */

namespace
{
    using namespace ID::Bench::Fixture;
    using namespace ID::Bench::Fixture::Cases;

#if defined(FIXTURE_CASE_LEGAL)
    consteval auto make_case_plan() noexcept
    {
        constexpr auto declaration_gamma = declare_dependencies(gamma(), std::tuple{ pair_of(kNameAlpha, kNameBeta) });
        constexpr auto declaration_alpha = declare_dependencies(alpha(), std::tuple<>{});
        constexpr auto declaration_beta  = declare_dependencies(beta(), std::tuple<>{});
        constexpr std::array<PluginIdentity, 2> selected{ alpha(), gamma() };
        return sort_and_validate(std::tuple{ declaration_gamma, declaration_alpha, declaration_beta }, selected);
    }
#else
    consteval auto make_case_plan() noexcept
    {
        constexpr auto declaration_gamma = declare_dependencies(gamma(), std::tuple{ pair_of(kNameAlpha, kNameAlpha) });
        constexpr auto declaration_alpha = declare_dependencies(alpha(), std::tuple<>{});
        constexpr std::array<PluginIdentity, 2> selected{ alpha(), gamma() };
        return sort_and_validate(std::tuple{ declaration_gamma, declaration_alpha }, selected);
    }
#endif

    constexpr auto kPlan = make_case_plan();

#if defined(FIXTURE_CASE_LEGAL)
    static_assert(kPlan.error == ErrorCode::None, "duplicate_candidate: 合法对照必须组装成功");
#else
    ID_FIXTURE_EXPECT_ERROR(kPlan, DuplicateCandidate);
#endif
} // namespace
