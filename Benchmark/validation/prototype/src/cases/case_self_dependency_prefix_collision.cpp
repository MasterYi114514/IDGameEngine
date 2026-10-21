#include "CaseCommon.hpp"

#include "FixtureTestSupport.hpp"

/**
 *  前 16 字节相同、64 位哈希相同、但完整名字不同的候选**不得**被判成自依赖（计划书 5.3 / 5.2）
 *  合法对照：同样的两个名字只用真实哈希，A 依赖 B 必须组装成功。
 *  @note 旧实现的自依赖判等只比 (长度, ID, prefix0, prefix1)，对超过 16 字节的名字等价于
 *        "前缀 + 哈希相同即同名"，于是真实哈希碰撞会被误报成 SelfDependency；
 *        现在必须完整名字相等才算自依赖，剩下的碰撞按 NameHashCollision 诊断。
 */

namespace
{
    using namespace ID::Bench::Fixture;
    using namespace ID::Bench::Fixture::Cases;
    using namespace ID::Bench::Fixture::TestSupport;

    // "ID.Case.PrefixSh" 恰好 16 字节，第 17 字节才不同：旧实现只看前缀字，因此会误判成同名
    inline constexpr char kNamePrefixSelf[]  = "ID.Case.PrefixShA";
    inline constexpr char kNamePrefixOther[] = "ID.Case.PrefixShB";

    // 夹具形状必须真的成立：等长、前 16 字节相同、第 17 字节起不同
    consteval bool same_first_16_bytes() noexcept
    {
        for (size_t i = 0; i < 16; ++i)
        {
            if (kNamePrefixSelf[i] != kNamePrefixOther[i])
            {
                return false;
            }
        }
        return kNamePrefixSelf[16] != kNamePrefixOther[16];
    }

    static_assert(sizeof(kNamePrefixSelf) == sizeof(kNamePrefixOther), "夹具：两个名字必须等长");
    static_assert(same_first_16_bytes(), "夹具：两个名字的前 16 字节必须相同，第 17 字节必须不同");
    static_assert(sizeof(kNamePrefixSelf) - 1 > 16, "夹具：名字必须长于 16 字节");

    consteval auto make_case_plan() noexcept
    {
        constexpr auto self       = register_plugin(kNamePrefixSelf);
        constexpr auto other      = register_plugin(kNamePrefixOther);
        constexpr auto other_decl = declare_dependencies(other, std::tuple<>{});
        auto           owner_decl = declare_dependencies(self, std::tuple{ group_of(kNamePrefixOther) });
#if !defined(FIXTURE_CASE_LEGAL)
        // 让候选携带"自身真实 ID"，名字却是另一个：模拟"前 16 字节相同 + 哈希碰撞 + 不同名"
        force_candidate_id(owner_decl, 0, self.id);
#endif
        constexpr std::array<PluginIdentity, 2> selected{ self, other };
        return sort_and_validate(std::tuple{ owner_decl, other_decl }, selected);
    }

    constexpr auto kPlan = make_case_plan();

#if defined(FIXTURE_CASE_LEGAL)
    // 区分力证明：同一 TU 里强制 ID 之后必须变成碰撞类别，而不是自依赖
    consteval bool forced_id_is_collision() noexcept
    {
        constexpr auto self       = register_plugin(kNamePrefixSelf);
        constexpr auto other      = register_plugin(kNamePrefixOther);
        constexpr auto other_decl = declare_dependencies(other, std::tuple<>{});
        auto           decl       = declare_dependencies(self, std::tuple{ group_of(kNamePrefixOther) });
        force_candidate_id(decl, 0, self.id);
        const auto plan = sort_and_validate(std::tuple{ decl, other_decl },
                                            std::array<PluginIdentity, 2>{ self, other });
        return plan.error == ErrorCode::NameHashCollision;
    }

    static_assert(kPlan.error == ErrorCode::None, "self_dependency_prefix_collision: 合法对照必须组装成功");
    static_assert(kPlan.active_count == 2, "self_dependency_prefix_collision: 合法对照必须有 2 个 active");
    static_assert(kPlan.edge_count == 1, "self_dependency_prefix_collision: 合法对照必须只有一条依赖边");
    static_assert(forced_id_is_collision(),
                  "self_dependency_prefix_collision: 同前缀同哈希不同名必须是碰撞，不能是自依赖");
#else
    ID_FIXTURE_EXPECT_ERROR(kPlan, NameHashCollision);
#endif
} // namespace
