#include "CaseCommon.hpp"

#include "FixtureTestSupport.hpp"

/**
 *  名字长度界限：96 字节（kMaxNameBytes）合法，97 字节必须拒绝（计划书 5.2 / 6.5）
 *  合法对照：同一 TU 里 96 字节名字必须组装成功，并断言 16/17 字节这类跨前缀字边界也合法。
 *  @note 名字由 kMaxNameBytes 决定，长度上限是契约不是经验值；本用例把
 *        "恰好上限" 与 "上限 + 1" 两档都真实编译一遍，避免边界实现被悄悄放宽或收紧。
 */

namespace
{
    using namespace ID::Bench::Fixture;
    using namespace ID::Bench::Fixture::Cases;
    using namespace ID::Bench::Fixture::TestSupport;

    // 名字缓冲：首字符为字母，其余全是合法尾字符；容量按"上限 + 1 字节 NUL + 余量"预留
    struct NameBuffer
    {
        char     data[kMaxNameBytes + 2] = {};
        uint32_t length                  = 0;
    };

    consteval NameBuffer make_long_name(size_t total) noexcept
    {
        NameBuffer name{};
        name.data[0] = 'A';                                  // 首字符必须是字母
        for (size_t i = 1; i < total; ++i)
        {
            name.data[i] = (i % 7 == 0) ? '.' : 'a';         // 尾字符取合法集合里的两种
        }
        name.data[total] = '\0';
        name.length      = static_cast<uint32_t>(total);
        return name;
    }

    inline constexpr NameBuffer kName16 = make_long_name(16);                    // 恰好覆盖两个前缀字
    inline constexpr NameBuffer kName17 = make_long_name(17);                    // 需要一次字节兜底比较
    inline constexpr NameBuffer kName96 = make_long_name(kMaxNameBytes);         // 合法上限
    inline constexpr NameBuffer kName97 = make_long_name(kMaxNameBytes + 1);     // 越界一字节

    consteval PluginIdentity identity_of(const NameBuffer& name) noexcept
    {
        return make_identity_from_text(name.data, name.length);
    }

#if defined(FIXTURE_CASE_LEGAL)
    consteval auto make_case_plan() noexcept
    {
        constexpr auto identity = identity_of(kName96);
        constexpr auto declaration = declare_dependencies(identity, std::tuple<>{});
        constexpr std::array<PluginIdentity, 1> selected{ identity };
        return sort_and_validate(std::tuple{ declaration }, selected);
    }

    constexpr auto kPlan = make_case_plan();

    static_assert(identity_of(kName96).is_valid, "name_boundaries: 96 字节名字必须是合法身份");
    static_assert(identity_of(kName16).is_valid && identity_of(kName17).is_valid,
                  "name_boundaries: 16/17 字节名字必须合法（前缀字边界不得误拒）");
    static_assert(!identity_of(kName97).is_valid, "name_boundaries: 97 字节名字必须被拒绝");
    static_assert(kPlan.error == ErrorCode::None, "name_boundaries: 96 字节名字必须组装成功");
    static_assert(kPlan.active_identity[0].length == kMaxNameBytes, "name_boundaries: 计划里必须保留完整长度");
#else
    consteval auto make_case_plan() noexcept
    {
        constexpr auto identity    = identity_of(kName97);      // 97 字节：超过上限一字节
        constexpr auto declaration = declare_dependencies(identity, std::tuple<>{});
        constexpr std::array<PluginIdentity, 1> selected{ identity };
        return sort_and_validate(std::tuple{ declaration }, selected);
    }

    constexpr auto kPlan = make_case_plan();

    // 越界输入必须是"身份非法"而不是其它兜底：先证明 96 字节合法，再要求类别
    static_assert(identity_of(kName96).is_valid, "name_boundaries: 96 字节名字必须是合法身份");
    ID_FIXTURE_EXPECT_ERROR(kPlan, InvalidName);
#endif
} // namespace
