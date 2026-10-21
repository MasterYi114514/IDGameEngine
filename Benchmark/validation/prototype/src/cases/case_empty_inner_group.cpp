#include "CaseCommon.hpp"

/**
 *  空内层：[[ ]] 是非法需求；空外层（没有任何需求）才是合法的（计划书 5.3）
 *  合法对照：同一插件改成"空外层"——std::tuple<>，表示没有上游。
 */

namespace
{
    using namespace ID::Bench::Fixture;
    using namespace ID::Bench::Fixture::Cases;

#if defined(FIXTURE_CASE_LEGAL)
    consteval auto make_case_plan() noexcept
    {
        constexpr auto declaration_alpha = declare_dependencies(alpha(), std::tuple<>{});    // 空外层：没有上游
        constexpr std::array<PluginIdentity, 1> selected{ alpha() };
        return sort_and_validate(std::tuple{ declaration_alpha }, selected);
    }
#else
    consteval auto make_case_plan() noexcept
    {
        // 空内层：一个需求组，但组里 0 个候选
        constexpr auto declaration_alpha = declare_dependencies(alpha(), std::tuple{ std::array<std::string_view, 0>{} });
        constexpr std::array<PluginIdentity, 1> selected{ alpha() };
        return sort_and_validate(std::tuple{ declaration_alpha }, selected);
    }
#endif

    constexpr auto kPlan = make_case_plan();

#if defined(FIXTURE_CASE_LEGAL)
    static_assert(kPlan.error == ErrorCode::None, "empty_inner_group: 合法对照必须组装成功");
    static_assert(kPlan.edge_count == 0, "空外层不得产生依赖边");
#else
    ID_FIXTURE_EXPECT_ERROR(kPlan, EmptyRequirementGroup);
#endif
} // namespace
