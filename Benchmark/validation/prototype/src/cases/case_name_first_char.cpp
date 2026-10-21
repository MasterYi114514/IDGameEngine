#include "CaseCommon.hpp"

/**
 *  名字首字符规则：必须是 ASCII 字母；数字或连字符开头必须拒绝（计划书 5.2）
 *  合法对照：首字符是字母、其余字符覆盖全部合法尾字符（数字 / 点 / 下划线 / 连字符）的名字。
 *  @note 只校验"尾字符集合"而漏掉"首字符"是常见实现漏洞，因此这里把两条非法首字符
 *        与一条覆盖全部尾字符的合法名字放在同一 TU 里对照。
 */

namespace
{
    using namespace ID::Bench::Fixture;
    using namespace ID::Bench::Fixture::Cases;

    inline constexpr char kNameAllTailChars[] = "Id.Case_9-x.y";     // 首字母 + 数字/点/下划线/连字符
    inline constexpr char kNameDigitFirst[]   = "9Case.Name";
    inline constexpr char kNameDashFirst[]    = "-Case.Name";

#if defined(FIXTURE_CASE_LEGAL)
    consteval auto make_case_plan() noexcept
    {
        constexpr auto identity    = register_plugin(kNameAllTailChars);
        constexpr auto declaration = declare_dependencies(identity, std::tuple<>{});
        constexpr std::array<PluginIdentity, 1> selected{ identity };
        return sort_and_validate(std::tuple{ declaration }, selected);
    }

    constexpr auto kPlan = make_case_plan();

    static_assert(kPlan.error == ErrorCode::None, "name_first_char: 覆盖全部合法尾字符的名字必须组装成功");
    static_assert(register_plugin(kNameAllTailChars).is_valid, "name_first_char: 合法名字的身份必须有效");
    static_assert(!register_plugin(kNameDigitFirst).is_valid, "name_first_char: 数字开头必须非法");
    static_assert(!register_plugin(kNameDashFirst).is_valid, "name_first_char: 连字符开头必须非法");
#else
    consteval auto make_case_plan() noexcept
    {
        constexpr auto identity    = register_plugin(kNameDigitFirst);      // 数字开头
        constexpr auto declaration = declare_dependencies(identity, std::tuple<>{});
        constexpr std::array<PluginIdentity, 1> selected{ identity };
        return sort_and_validate(std::tuple{ declaration }, selected);
    }

    constexpr auto kPlan = make_case_plan();

    static_assert(!register_plugin(kNameDashFirst).is_valid, "name_first_char: 连字符开头必须非法");
    ID_FIXTURE_EXPECT_ERROR(kPlan, InvalidName);
#endif
} // namespace
