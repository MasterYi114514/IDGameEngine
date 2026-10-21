#include "ProductionCaseSupport.hpp"

/**
 *  手工 POD 的 contract_count 越过本条目契约容量：InvalidDeclaration
 *  合法对照：同一条目不做篡改。
 *  用途：复核"契约数组容量被写坏时必须先于 contracts[group] 索引拒绝"的整改。
 */

namespace
{
    using namespace ID::Bench::Production;

    consteval auto make_tampered_entry() noexcept
    {
        auto entry                       = entry_leaf(beta());
        using Metadata                   = decltype(entry.metadata);
        entry.metadata.contract_count    = static_cast<uint16_t>(Metadata::kContractSlots + 1);
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
    static_assert(kPlan.diagnostic.code == PluginError::None, "pod_contract_count_capacity: 合法对照必须组装成功");
#else
    ID_PRODUCTION_EXPECT_ERROR(kPlan, InvalidDeclaration);
#endif
} // namespace
