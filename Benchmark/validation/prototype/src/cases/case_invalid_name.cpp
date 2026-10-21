#include "CaseCommon.hpp"

/**
 *  名字格式：首字符必须字母、只允许字母数字与 . _ -（计划书 5.2）
 *  合法对照：同一插件名去掉非法字符。
 */

namespace
{
    using namespace ID::Bench::Fixture;
    using namespace ID::Bench::Fixture::Cases;

#if defined(FIXTURE_CASE_LEGAL)
    consteval auto make_case_plan() noexcept
    {
        constexpr auto identity          = register_plugin(kNameAlpha);
        constexpr auto declaration_alpha = declare_dependencies(identity, std::tuple<>{});
        constexpr std::array<PluginIdentity, 1> selected{ identity };
        return sort_and_validate(std::tuple{ declaration_alpha }, selected);
    }
#else
    consteval auto make_case_plan() noexcept
    {
        constexpr auto identity          = register_plugin(kNameBad);     // 含 '!'，格式非法
        constexpr auto declaration_alpha = declare_dependencies(identity, std::tuple<>{});
        constexpr std::array<PluginIdentity, 1> selected{ identity };
        return sort_and_validate(std::tuple{ declaration_alpha }, selected);
    }
#endif

    constexpr auto kPlan = make_case_plan();

#if defined(FIXTURE_CASE_LEGAL)
    static_assert(kPlan.error == ErrorCode::None, "invalid_name: 合法对照必须组装成功");
    static_assert(kPlan.active_identity[0].is_valid, "合法对照的身份必须有效");
#else
    ID_FIXTURE_EXPECT_ERROR(kPlan, InvalidName);
#endif
} // namespace
