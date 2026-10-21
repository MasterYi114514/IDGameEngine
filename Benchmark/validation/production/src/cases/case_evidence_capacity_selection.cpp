#include "ProductionCaseSupport.hpp"

/**
 *  证据容量越界：selected_count 等于 active_count 但超过 Shape 的选中证据容量
 *  合法对照：同一 catalog/selected 用足够容量构造证据，计划必须合法。
 */

namespace
{
    using namespace ID::Bench::Production;

    inline constexpr auto kCatalog  = baseline_catalog();
    inline constexpr auto kSelected = baseline_selected();

    // 合法对照：容量 = 2
    using GoodShape                     = ShapeOf<Plain<decltype(kCatalog)>, 2, 2, 1, 1>;
    inline constexpr auto kGoodInput    = with_evidence(make_input<GoodShape>(kCatalog, kSelected));
    inline constexpr auto kGoodPlan     = ID::PluginManager::sort_and_validate(kGoodInput);

    // 反向：选中证据容量只有 1，但 active_count = 2
    using BadShape = ShapeOf<Plain<decltype(kCatalog)>, 2, 1, 1, 1>;

#if defined(PRODUCTION_CASE_LEGAL)
    static_assert(kGoodPlan.diagnostic.code == PluginError::None, "evidence_capacity_selection: 合法对照必须成立");
#else
    consteval auto make_case_input() noexcept
    {
        auto input = make_input<BadShape>(kCatalog, kSelected);
        input.evidence.selected_count = 2;      // == active_count，但 > 容量 1
        return input;
    }

    inline constexpr auto kPlan = ID::PluginManager::sort_and_validate(make_case_input());

    static_assert(kGoodPlan.diagnostic.code == PluginError::None, "evidence_capacity_selection: 合法对照必须成立");
    ID_PRODUCTION_EXPECT_ERROR(kPlan, EvidenceSelectionMismatch);
#endif
} // namespace
