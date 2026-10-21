#include "ProductionCaseSupport.hpp"

/**
 *  证据容量越界：edge_count 等于真实边数但超过 Shape 的边证据容量
 *  合法对照：同一输入用足够容量构造证据，计划必须合法。
 */

namespace
{
    using namespace ID::Bench::Production;

    inline constexpr auto kCatalog  = baseline3_catalog();
    inline constexpr auto kSelected = baseline3_selected();

    using GoodShape                  = ShapeOf<Plain<decltype(kCatalog)>, 3, 3, 2, 2, 3>;
    inline constexpr auto kGoodInput = with_evidence(make_input<GoodShape>(kCatalog, kSelected));
    inline constexpr auto kGoodPlan  = ID::PluginManager::sort_and_validate(kGoodInput);

    using BadShape = ShapeOf<Plain<decltype(kCatalog)>, 3, 3, 2, 1, 3>;

#if defined(PRODUCTION_CASE_LEGAL)
    static_assert(kGoodPlan.diagnostic.code == PluginError::None, "evidence_capacity_edge: 合法对照必须成立");
#else
    consteval auto make_case_input() noexcept
    {
        auto input = make_input<BadShape>(kCatalog, kSelected);
        for (size_t i = 0; i < 3; ++i)
        {
            input.evidence.selected[i] = kGoodInput.evidence.selected[i];
        }
        for (size_t i = 0; i < 2; ++i)
        {
            input.evidence.bindings[i] = kGoodInput.evidence.bindings[i];
        }
        input.evidence.selected_count = 3;
        input.evidence.binding_count  = 2;
        input.evidence.edge_count     = 2;      // 真实边数 2，但容量只有 1
        return input;
    }

    inline constexpr auto kPlan = ID::PluginManager::sort_and_validate(make_case_input());

    static_assert(kGoodPlan.diagnostic.code == PluginError::None, "evidence_capacity_edge: 合法对照必须成立");
    ID_PRODUCTION_EXPECT_ERROR(kPlan, EvidenceEdgeMismatch);
#endif
} // namespace
