#include "CaseCommon.hpp"

/**
 *  未知 active：选中集合里出现 catalog 之外的插件必须拒绝（计划书 7 产品选择）
 *  合法对照：把选中项换成 catalog 里真实存在的插件。
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
        // Ghost 从未出现在 catalog 中
        constexpr std::array<PluginIdentity, 2> selected{ alpha(), register_plugin(kNameGhost) };
        return sort_and_validate(std::tuple{ declaration_alpha }, selected);
    }
#endif

    constexpr auto kPlan = make_case_plan();

#if defined(FIXTURE_CASE_LEGAL)
    static_assert(kPlan.error == ErrorCode::None, "unknown_active: 合法对照必须组装成功");
    static_assert(kPlan.active_count == 2, "unknown_active: 合法对照有 2 个 active");
#else
    ID_FIXTURE_EXPECT_ERROR(kPlan, UnknownActive);
#endif
} // namespace
