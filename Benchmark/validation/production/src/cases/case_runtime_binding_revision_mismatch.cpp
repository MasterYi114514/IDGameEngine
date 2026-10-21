#include "ProductionCaseSupport.hpp"

/**
 *  运行期绑定记录的接口修订与计划不一致：RuntimeBindingMismatch（计划书 10.2）
 *  合法对照：原样运行期元数据必须一致。
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
    static_assert(kBasePlan.diagnostic.code == PluginError::None, "runtime_binding_revision_mismatch: 原样证据必须合法");
#else
    consteval auto make_case_input() noexcept
    {
        auto input = kInput;
        input.evidence.runtime[0].api_revision = 2;      // 计划里的 API 修订是 1
        return input;
    }

    inline constexpr auto kPlan = ID::PluginManager::sort_and_validate(make_case_input());

    static_assert(kBasePlan.diagnostic.code == PluginError::None, "runtime_binding_revision_mismatch: 基线必须合法");
    ID_PRODUCTION_EXPECT_ERROR(kPlan, RuntimeBindingMismatch);
#endif
} // namespace
