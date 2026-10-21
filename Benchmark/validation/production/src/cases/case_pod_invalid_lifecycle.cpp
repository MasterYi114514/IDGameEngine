#include "ProductionCaseSupport.hpp"

/**
 *  手工 POD 的 lifecycle 不在合法枚举集合内（9）：InvalidDeclaration
 *  合法对照：同一条目不做篡改。
 *  用途：复核"非法 lifecycle 不能被静默当成 Standard/None 继续组装"的整改。
 */

namespace
{
    using namespace ID::Bench::Production;

    consteval auto make_tampered_entry() noexcept
    {
        auto entry                 = entry_leaf(beta());
        entry.metadata.lifecycle   = static_cast<PluginLifecycle>(9);
        return entry;
    }

#if defined(PRODUCTION_CASE_LEGAL)
    inline constexpr auto kCatalog  = std::tuple{ entry_leaf(beta()) };
#else
    inline constexpr auto kCatalog  = std::tuple{ make_tampered_entry() };
#endif

    inline constexpr auto kSelected = std::array<PluginIdentity, 1>{ beta() };
    using Shape                     = ShapeOf<Plain<decltype(kCatalog)>, 1, 1, 1, 1>;
    inline constexpr auto kPlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<Shape>(kCatalog, kSelected)));

#if defined(PRODUCTION_CASE_LEGAL)
    static_assert(kPlan.diagnostic.code == PluginError::None, "pod_invalid_lifecycle: 合法对照必须组装成功");
#else
    ID_PRODUCTION_EXPECT_ERROR(kPlan, InvalidDeclaration);
#endif
} // namespace
