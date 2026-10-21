#include "ProductionCaseSupport.hpp"

/**
 *  重复的选中证据不能靠"数量相等"顶替另一条记录：双向一一匹配必须拒绝（计划书 8.5）
 *  合法对照：原样三条 selected 证据逐条消费。
 */

namespace
{
    using namespace ID::Bench::Production;

    inline constexpr auto kCatalog  = baseline3_catalog();
    inline constexpr auto kSelected = baseline3_selected();
    using Shape                     = ShapeOf<Plain<decltype(kCatalog)>, 3, 3, 2, 2, 3>;
    inline constexpr auto kInput    = with_evidence(make_input<Shape>(kCatalog, kSelected));
    inline constexpr auto kBasePlan = ID::PluginManager::sort_and_validate(kInput);

#if defined(PRODUCTION_CASE_LEGAL)
    static_assert(kBasePlan.diagnostic.code == PluginError::None, "evidence_duplicate_selection: 原样证据必须合法");
#else
    consteval auto make_case_input() noexcept
    {
        auto input = kInput;
        // 三条记录仍占三个位置，但 Beta 被 Alpha 顶替
        input.evidence.selected[1] = input.evidence.selected[0];
        return input;
    }

    inline constexpr auto kPlan = ID::PluginManager::sort_and_validate(make_case_input());

    static_assert(kBasePlan.diagnostic.code == PluginError::None, "evidence_duplicate_selection: 基线必须合法");
    ID_PRODUCTION_EXPECT_ERROR(kPlan, EvidenceSelectionMismatch);
#endif
} // namespace
