#include "CaseCommon.hpp"

/**
 *  自依赖：候选名包含所属插件自身时必须拒绝（计划书 5.3 / 16.1）
 *  合法对照：A 依赖 B（两个不同插件），同一 TU、同一环境。
 */

namespace
{
    using namespace ID::Bench::Fixture;
    using namespace ID::Bench::Fixture::Cases;

#if defined(FIXTURE_CASE_LEGAL)
    consteval auto make_case_plan() noexcept
    {
        constexpr auto declaration_alpha = declare_dependencies(alpha(), std::tuple{ group_of(kNameBeta) });
        constexpr auto declaration_beta  = declare_dependencies(beta(), std::tuple<>{});
        constexpr std::array<PluginIdentity, 2> selected{ beta(), alpha() };    // 故意乱序，验证 canonical 排序
        return sort_and_validate(std::tuple{ declaration_alpha, declaration_beta }, selected);
    }
#else
    consteval auto make_case_plan() noexcept
    {
        constexpr auto declaration_alpha = declare_dependencies(alpha(), std::tuple{ group_of(kNameAlpha) });   // 自己依赖自己
        constexpr std::array<PluginIdentity, 1> selected{ alpha() };
        return sort_and_validate(std::tuple{ declaration_alpha }, selected);
    }
#endif

    constexpr auto kPlan = make_case_plan();

#if defined(FIXTURE_CASE_LEGAL)
    static_assert(kPlan.error == ErrorCode::None, "self_dependency: 合法对照必须组装成功");
#else
    ID_FIXTURE_EXPECT_ERROR(kPlan, SelfDependency);
#endif
} // namespace
