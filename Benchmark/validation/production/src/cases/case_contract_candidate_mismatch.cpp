#include "ProductionCaseSupport.hpp"

/**
 *  逐候选修订记录必须与候选名集合一一对应（计划书 7 接口契约）
 *  合法对照：修订记录名字与唯一候选一致。
 */

namespace
{
    using namespace ID::Bench::Production;

#if defined(PRODUCTION_CASE_LEGAL)
    inline constexpr auto kCatalog = std::tuple{ entry_leaf(beta()), entry_dep(alpha(), kNameBeta) };
#else
    consteval auto make_catalog() noexcept
    {
        auto alpha_entry = entry_dep(alpha(), kNameBeta);
        alpha_entry.metadata.revisions[0].name = kNameGamma;      // 记录名不在候选集合里
        return std::tuple{ entry_leaf(beta()), alpha_entry };
    }

    inline constexpr auto kCatalog = make_catalog();
#endif

    inline constexpr auto kSelected = std::array<PluginIdentity, 2>{ alpha(), beta() };
    using Shape                     = ShapeOf<Plain<decltype(kCatalog)>, 2, 2, 1, 1>;
    inline constexpr auto kPlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<Shape>(kCatalog, kSelected)));

#if defined(PRODUCTION_CASE_LEGAL)
    static_assert(kPlan.diagnostic.code == PluginError::None, "contract_candidate_mismatch: 合法对照必须组装成功");
#else
    ID_PRODUCTION_EXPECT_ERROR(kPlan, ContractCandidateMismatch);
#endif
} // namespace
