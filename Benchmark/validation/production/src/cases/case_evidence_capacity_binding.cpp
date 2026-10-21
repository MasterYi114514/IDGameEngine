#include "ProductionCaseSupport.hpp"

/**
 *  证据容量越界：binding_count 等于真实绑定数但超过 Shape 的绑定证据容量
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

    using BadShape = ShapeOf<Plain<decltype(kCatalog)>, 3, 3, 1, 2, 3>;

#if defined(PRODUCTION_CASE_LEGAL)
    static_assert(kGoodPlan.diagnostic.code == PluginError::None, "evidence_capacity_binding: 合法对照必须成立");
#else
    consteval auto make_case_input() noexcept
    {
        auto input = make_input<BadShape>(kCatalog, kSelected);
        // 先让选中证据通过，才能证明拒绝原因是"绑定证据容量"而不是更早的选择证据
        for (size_t i = 0; i < 3; ++i)
        {
            input.evidence.selected[i] = kGoodInput.evidence.selected[i];
        }
        input.evidence.selected_count = 3;
        input.evidence.binding_count  = 2;      // 真实绑定数 2，但容量只有 1
        return input;
    }

    inline constexpr auto kPlan = ID::PluginManager::sort_and_validate(make_case_input());

    static_assert(kGoodPlan.diagnostic.code == PluginError::None, "evidence_capacity_binding: 合法对照必须成立");
    ID_PRODUCTION_EXPECT_ERROR(kPlan, EvidenceProviderMismatch);
#endif
} // namespace
