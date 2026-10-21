#include "ProductionCaseSupport.hpp"

/**
 *  未知候选：白名单里的名字不在完整 catalog 中必须拒绝（未选中插件同样校验）
 *  合法对照：候选 Beta 在 catalog 中，且本产品选中 Beta。
 */

namespace
{
    using namespace ID::Bench::Production;

#if defined(PRODUCTION_CASE_LEGAL)
    inline constexpr auto kCatalog  = std::tuple{ entry_leaf(beta()), entry_dep(alpha(), kNameBeta) };
    inline constexpr auto kSelected = std::array<PluginIdentity, 2>{ alpha(), beta() };
#else
    // 契约能力仍是合法的 CapB；候选名 Ghost 不在 catalog 中
    inline constexpr auto kCatalog = std::tuple{
        make_entry(alpha(), std::tuple{ group1(kNameGhost) }, std::array<std::string_view, 1>{ kCapBeta },
                   kTargetAlpha, kNamespaceAlpha, provides1(kCapAlpha, 1, 0)) };
    inline constexpr auto kSelected = std::array<PluginIdentity, 1>{ alpha() };
#endif

#if defined(PRODUCTION_CASE_LEGAL)
    using Shape = ShapeOf<Plain<decltype(kCatalog)>, 2, 2, 1, 1>;
#else
    using Shape = ShapeOf<Plain<decltype(kCatalog)>, 1, 1, 1, 1>;
#endif

    inline constexpr auto kPlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<Shape>(kCatalog, kSelected)));

#if defined(PRODUCTION_CASE_LEGAL)
    static_assert(kPlan.diagnostic.code == PluginError::None, "unknown_candidate: 合法对照必须组装成功");
#else
    ID_PRODUCTION_EXPECT_ERROR(kPlan, UnknownCandidate);
#endif
} // namespace
