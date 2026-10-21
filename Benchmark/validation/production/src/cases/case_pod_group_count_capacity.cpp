#include "ProductionCaseSupport.hpp"

/**
 *  手工 POD 的 group_count 越过本条目形状容量：InvalidDeclaration
 *  合法对照：同一条目不做篡改。
 *  用途：复核"坏 group_count 必须在任何 group_begin/contracts 索引之前被拒绝"的整改。
 */

namespace
{
    using namespace ID::Bench::Production;

    consteval auto make_tampered_entry() noexcept
    {
        auto entry                    = entry_leaf(beta());
        using Declaration             = decltype(entry.declaration);
        entry.declaration.group_count = static_cast<uint16_t>(Declaration::kGroupCapacity + 1);
        return entry;
    }

#if defined(PRODUCTION_CASE_LEGAL)
    inline constexpr auto kCatalog = std::tuple{ entry_leaf(beta()) };
#else
    inline constexpr auto kCatalog = std::tuple{ make_tampered_entry() };
#endif

    inline constexpr auto kSelected = std::array<PluginIdentity, 1>{ beta() };
    using Shape                     = ShapeOf<Plain<decltype(kCatalog)>, 1, 1, 1, 1>;
    inline constexpr auto kPlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<Shape>(kCatalog, kSelected)));

#if defined(PRODUCTION_CASE_LEGAL)
    static_assert(kPlan.diagnostic.code == PluginError::None, "pod_group_count_capacity: 合法对照必须组装成功");
#else
    ID_PRODUCTION_EXPECT_ERROR(kPlan, InvalidDeclaration);
#endif
} // namespace
