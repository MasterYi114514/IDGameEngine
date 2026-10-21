#include "ProductionCaseSupport.hpp"
#include "ProductionPositiveProbes.hpp"

/**
 *  驱动器自检：被"篡改"的正向断言必须让编译失败
 *  1. 本 TU 与 positive_basics 用同一份生产 API 输入与形状，只把一条断言改成错误期望；
 *  2. 它单独编成一个 EXCLUDE_FROM_ALL 目标，由 RunProductionCases.cmake 构建并要求
 *     "恰好 1 条 static assertion failed"；如果正向断言没有真的被求值，本目标会静默通过，
 *     自检就会失败，从而暴露"正向 TU 没有被真正编译/断言"的问题；
 *  3. 本 TU 不进入 IDProductionPositiveCases，也不进入任何可运行程序。
 */

namespace
{
    using namespace ID::Bench::Production;

    inline constexpr auto kCatalog  = std::tuple{ entry_leaf(gamma()), entry_leaf(alpha()), entry_leaf(beta()) };
    inline constexpr auto kSelected = std::array<PluginIdentity, 3>{ gamma(), beta(), alpha() };
    using Shape                     = ShapeOf<Plain<decltype(kCatalog)>, 3, 3, 1, 1>;
    inline constexpr auto kPlan =
        ID::PluginManager::sort_and_validate(with_evidence(make_input<Shape>(kCatalog, kSelected)));

    static_assert(kPlan.diagnostic.code == PluginError::None, "mutated_assert_probe: 前置计划必须合法");

    // 故意写错的期望：真实值是 3，这里断言 127，编译必须失败且只有这一条断言失败
    static_assert(kPlan.active_count == 127, "ID_POSITIVE_MUTATED_ASSERT_DETECTED: 篡改的 active_count 期望");
} // namespace
