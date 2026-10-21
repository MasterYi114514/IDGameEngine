#include "ProductionCaseSupport.hpp"

/**
 *  构建证据记录的插件身份（64 位 ID）与推导不一致：EvidenceIdentityMismatch（计划书 8.5）
 *  合法对照：原样证据必须一致。
 */

namespace
{
    using namespace ID::Bench::Production;

    inline constexpr auto kCatalog  = baseline_catalog();
    inline constexpr auto kSelected = baseline_selected();
    using Shape                     = ShapeOf<Plain<decltype(kCatalog)>, 2, 2, 1, 1>;
    inline constexpr auto kInput    = with_evidence(make_input<Shape>(kCatalog, kSelected));
    inline constexpr auto kBasePlan = ID::PluginManager::sort_and_validate(kInput);

#if defined(PRODUCTION_CASE_LEGAL)
    static_assert(kBasePlan.diagnostic.code == PluginError::None, "evidence_identity_mismatch: 原样证据必须合法");
#else
    consteval auto make_case_input() noexcept
    {
        auto input = kInput;
        input.evidence.selected[0].plugin_id ^= 1ull;      // 名字与 target 不动，只改身份
        return input;
    }

    inline constexpr auto kPlan = ID::PluginManager::sort_and_validate(make_case_input());

    static_assert(kBasePlan.diagnostic.code == PluginError::None, "evidence_identity_mismatch: 基线必须合法");
    ID_PRODUCTION_EXPECT_ERROR(kPlan, EvidenceIdentityMismatch);
#endif
} // namespace
