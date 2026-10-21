#include "CaseCommon.hpp"

/**
 *  名字内部出现 NUL：必须以 InvalidName 拒绝，不能把 NUL 当成名字结束悄悄截断（计划书 5.2）
 *  合法对照：同一个可见前缀后没有内嵌 NUL 的名字必须组装成功。
 *  @note register_plugin 以数组长度 N-1 作为名字长度，因此内部 NUL 会作为"字符"送进校验；
 *        NUL 既不是字母数字也不是 . _ -，必须被判非法，而不能被当成终止符。
 */

namespace
{
    using namespace ID::Bench::Fixture;
    using namespace ID::Bench::Fixture::Cases;

    // 13 个可见字符 + 1 个内嵌 NUL + 4 个可见字符 = 18 字节，再加末尾 NUL
    inline constexpr char kNameInternalNul[19] = "ID.Case.Alpha\0Beta";

    consteval bool has_internal_nul() noexcept
    {
        return kNameInternalNul[13] == '\0' && kNameInternalNul[17] == 'a';
    }

    static_assert(has_internal_nul(), "夹具：名字第 14 字节必须是内嵌 NUL，且其后仍有可见字符");

#if defined(FIXTURE_CASE_LEGAL)
    consteval auto make_case_plan() noexcept
    {
        constexpr auto identity    = register_plugin("ID.Case.Alpha");     // 无内嵌 NUL
        constexpr auto declaration = declare_dependencies(identity, std::tuple<>{});
        constexpr std::array<PluginIdentity, 1> selected{ identity };
        return sort_and_validate(std::tuple{ declaration }, selected);
    }

    constexpr auto kPlan = make_case_plan();

    static_assert(kPlan.error == ErrorCode::None, "name_internal_nul: 合法对照必须组装成功");
    static_assert(kPlan.active_identity[0].view() == "ID.Case.Alpha", "name_internal_nul: 合法对照名字必须完整");
#else
    consteval auto make_case_plan() noexcept
    {
        constexpr auto identity    = register_plugin(kNameInternalNul);    // 长度取数组长度 - 1，含内部 NUL
        constexpr auto declaration = declare_dependencies(identity, std::tuple<>{});
        constexpr std::array<PluginIdentity, 1> selected{ identity };
        return sort_and_validate(std::tuple{ declaration }, selected);
    }

    constexpr auto kPlan = make_case_plan();

    static_assert(!register_plugin(kNameInternalNul).is_valid, "name_internal_nul: 内嵌 NUL 必须使身份非法");
    ID_FIXTURE_EXPECT_ERROR(kPlan, InvalidName);
#endif
} // namespace
