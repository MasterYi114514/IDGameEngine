#include "ProductionCaseSupport.hpp"

/**
 *  碰撞模拟：名字不同但 64 位 ID 相同必须诊断为 NameHashCollision（计划书 5.2 / 16.1）
 *  合法对照：两个条目使用真实哈希，名字与 ID 都不同。
 *  @note 生产 register_plugin 只计算真实哈希；本用例按允许的测试夹具规则，
 *        在 register_plugin 的返回值上强制覆盖 id，不新增任何公共自定义 ID 入口。
 */

namespace
{
    using namespace ID::Bench::Production;

    consteval PluginIdentity forced_beta() noexcept
    {
        PluginIdentity identity = beta();
        identity.id             = alpha().id;      // 名字仍是 ID.Case.Beta，ID 被强制成 Alpha 的
        return identity;
    }

#if defined(PRODUCTION_CASE_LEGAL)
    inline constexpr auto kCatalog  = std::tuple{ entry_leaf(alpha()), entry_leaf(beta()) };
    inline constexpr auto kSelected = std::array<PluginIdentity, 2>{ alpha(), beta() };
#else
    inline constexpr auto kCatalog  = std::tuple{ entry_leaf(alpha()), entry_leaf(forced_beta()) };
    inline constexpr auto kSelected = std::array<PluginIdentity, 2>{ alpha(), beta() };
#endif

    using Shape = ShapeOf<Plain<decltype(kCatalog)>, 2, 2, 1, 1>;

    inline constexpr auto kPlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<Shape>(kCatalog, kSelected)));

#if defined(PRODUCTION_CASE_LEGAL)
    static_assert(kPlan.diagnostic.code == PluginError::None, "hash_collision: 合法对照必须组装成功");
#else
    // 碰撞夹具自身必须是合法名字：先证明不是 InvalidName，再要求类别
    static_assert(forced_beta().is_valid, "hash_collision: 碰撞夹具自身必须是合法名字");
    ID_PRODUCTION_EXPECT_ERROR(kPlan, NameHashCollision);
#endif
} // namespace
