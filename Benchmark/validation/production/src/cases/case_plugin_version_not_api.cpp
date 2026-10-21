#include "ProductionCaseSupport.hpp"

/**
 *  插件发布版本不能冒充接口修订（计划书 4.3）
 *  负向：提供者 version=99 但 api_revision=1，契约要求 2..2 → RevisionMismatch。
 *  合法对照：version=99 且 api_revision=2，契约 2..2 → 成功。
 */

namespace
{
    using namespace ID::Bench::Production;

#if defined(PRODUCTION_CASE_LEGAL)
    inline constexpr auto kBetaRevision = 2;
#else
    inline constexpr auto kBetaRevision = 1;
#endif

    consteval auto make_catalog() noexcept
    {
        auto beta_entry = make_entry(beta(), std::tuple<>{}, std::array<std::string_view, 0>{}, kTargetBeta,
                                     kNamespaceBeta, provides1(kCapBeta, kBetaRevision, 1),
                                     PluginKind::Module, PluginLifecycle::Standard, 99);
        auto alpha_entry = entry_dep(alpha(), kNameBeta, 1, 2, 2);
        return std::tuple{ beta_entry, alpha_entry };
    }

    inline constexpr auto kCatalog  = make_catalog();
    inline constexpr auto kSelected = std::array<PluginIdentity, 2>{ alpha(), beta() };
    using Shape                     = ShapeOf<Plain<decltype(kCatalog)>, 2, 2, 1, 1>;
    inline constexpr auto kPlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<Shape>(kCatalog, kSelected)));

#if defined(PRODUCTION_CASE_LEGAL)
    static_assert(kPlan.diagnostic.code == PluginError::None, "plugin_version_not_api: 合法对照必须组装成功");
    static_assert(kPlan.provision_api_revision[kPlan.provision_begin[kPlan.find_active("ID.Case.Beta")]] == 2,
                  "plugin_version_not_api: 计划里必须记录 API 修订 2");
#else
    ID_PRODUCTION_EXPECT_ERROR(kPlan, RevisionMismatch);
#endif
} // namespace
