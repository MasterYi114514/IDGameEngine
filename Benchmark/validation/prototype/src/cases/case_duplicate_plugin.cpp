#include "CaseCommon.hpp"

/**
 *  catalog 重名：同一插件名声明两次必须拒绝（计划书 7 身份校验）
 *  合法对照：两个不同名字的插件。
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
        // 同名同 ID 的两个声明条目：catalog 身份必须唯一
        constexpr auto declaration_first  = declare_dependencies(alpha(), std::tuple<>{});
        constexpr auto declaration_second = declare_dependencies(register_plugin(kNameAlpha), std::tuple<>{});
        constexpr std::array<PluginIdentity, 1> selected{ alpha() };
        return sort_and_validate(std::tuple{ declaration_first, declaration_second }, selected);
    }
#endif

    constexpr auto kPlan = make_case_plan();

#if defined(FIXTURE_CASE_LEGAL)
    static_assert(kPlan.error == ErrorCode::None, "duplicate_plugin: 合法对照必须组装成功");
#else
    ID_FIXTURE_EXPECT_ERROR(kPlan, DuplicatePlugin);
#endif
} // namespace
