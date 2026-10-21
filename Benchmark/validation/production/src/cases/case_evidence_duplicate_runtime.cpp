#include "ProductionCaseSupport.hpp"

/**
 *  重复的运行期绑定记录不能顶替另一条能力：双向一一匹配必须拒绝（计划书 10.2）
 *  合法对照：原样三条运行期记录逐条消费。
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
    static_assert(kBasePlan.diagnostic.code == PluginError::None, "evidence_duplicate_runtime: 原样证据必须合法");
#else
    consteval auto make_case_input() noexcept
    {
        auto input = kInput;
        input.evidence.runtime[2] = input.evidence.runtime[0];      // Gamma 的能力记录被 Alpha 顶替
        return input;
    }

    inline constexpr auto kPlan = ID::PluginManager::sort_and_validate(make_case_input());

    static_assert(kBasePlan.diagnostic.code == PluginError::None, "evidence_duplicate_runtime: 基线必须合法");
    ID_PRODUCTION_EXPECT_ERROR(kPlan, RuntimeBindingMismatch);
#endif
} // namespace
