#include "ProductionCaseSupport.hpp"

/**
 *  手工 POD 的候选名字池越界（offset 指向池尾、长度越过池界）：InvalidDeclaration
 *  合法对照：同一条目不做篡改。
 *  用途：复核"名字存不进本条目字节池时必须结构化拒绝，不许越界读池"的整改。
 */

namespace
{
    using namespace ID::Bench::Production;

    consteval auto make_tampered_entry() noexcept
    {
        auto entry                       = entry_dep(alpha(), kNameBeta);
        using Declaration                = decltype(entry.declaration);
        entry.declaration.candidate_offset[0] = static_cast<uint32_t>(Declaration::kPoolBytes);
        entry.declaration.candidate_length[0] = 1;
        return entry;
    }

#if defined(PRODUCTION_CASE_LEGAL)
    inline constexpr auto kCatalog  = std::tuple{ entry_leaf(beta()), entry_dep(alpha(), kNameBeta) };
    inline constexpr auto kSelected = std::array<PluginIdentity, 2>{ alpha(), beta() };
#else
    inline constexpr auto kCatalog  = std::tuple{ entry_leaf(beta()), make_tampered_entry() };
    inline constexpr auto kSelected = std::array<PluginIdentity, 2>{ alpha(), beta() };
#endif

    using Shape                     = ShapeOf<Plain<decltype(kCatalog)>, 2, 2, 1, 1>;
    inline constexpr auto kPlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<Shape>(kCatalog, kSelected)));

#if defined(PRODUCTION_CASE_LEGAL)
    static_assert(kPlan.diagnostic.code == PluginError::None, "pod_pool_bounds: 合法对照必须组装成功");
#else
    ID_PRODUCTION_EXPECT_ERROR(kPlan, InvalidDeclaration);
#endif
} // namespace
