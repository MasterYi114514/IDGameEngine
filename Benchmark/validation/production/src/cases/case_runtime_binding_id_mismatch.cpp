#include "ProductionCaseSupport.hpp"

/**
 *  运行期绑定记录的插件身份与计划不一致：RuntimeBindingMismatch（计划书 5.4 / 10.2）
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
    static_assert(kBasePlan.diagnostic.code == PluginError::None, "runtime_binding_id_mismatch: 原样证据必须合法");
#else
    consteval auto make_case_input() noexcept
    {
        auto input = kInput;
        input.evidence.runtime[0].plugin_id ^= 1ull;      // 名字与能力不动，只改身份
        return input;
    }

    inline constexpr auto kPlan = ID::PluginManager::sort_and_validate(make_case_input());

    static_assert(kBasePlan.diagnostic.code == PluginError::None, "runtime_binding_id_mismatch: 基线必须合法");
    ID_PRODUCTION_EXPECT_ERROR(kPlan, RuntimeBindingMismatch);
#endif
} // namespace
