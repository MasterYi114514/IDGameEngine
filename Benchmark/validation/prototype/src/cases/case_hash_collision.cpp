#include "CaseCommon.hpp"

#include "FixtureTestSupport.hpp"

/**
 *  碰撞模拟：名字不同但 64 位 ID 相同必须诊断为 NameHashCollision，不能把哈希当成身份（计划书 5.2 / 16.1）
 *  合法对照：两个条目使用真实哈希，名字与 ID 都不同。
 *  @note 生产 register_plugin 只计算真实哈希；这里用 TestSupport 的强制 ID 构造输入，
 *        不新增任何公共自定义 ID 接口。
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
        // Beta 的名字保持合法，但 ID 被强制写成 Alpha 的真实 ID：不同名同哈希
        constexpr auto forced_beta = TestSupport::make_colliding_identity(kNameBeta, register_plugin(kNameAlpha).id);
        constexpr auto declaration_beta = declare_dependencies(forced_beta, std::tuple<>{});
        constexpr std::array<PluginIdentity, 2> selected{ alpha(), beta() };
        return sort_and_validate(std::tuple{ declaration_alpha, declaration_beta }, selected);
    }
#endif

    constexpr auto kPlan = make_case_plan();

#if defined(FIXTURE_CASE_LEGAL)
    static_assert(kPlan.error == ErrorCode::None, "hash_collision: 合法对照必须组装成功");
#else
    // 碰撞输入必须是"合法身份 + 强制相同 ID"：先证明不是 InvalidName，再要求类别是 NameHashCollision
    static_assert(TestSupport::make_colliding_identity(kNameBeta, register_plugin(kNameAlpha).id).is_valid,
                  "hash_collision: 碰撞夹具自身必须是合法名字");
    ID_FIXTURE_EXPECT_ERROR(kPlan, NameHashCollision);
#endif
} // namespace
