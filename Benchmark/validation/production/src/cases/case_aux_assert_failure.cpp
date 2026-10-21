#include "ProductionCaseSupport.hpp"

/**
 *  驱动器自检样本之二（不是被测契约，不计入反向用例数）
 *  类别断言写对（SelfDependency），但另有一条辅助断言故意失败：
 *  正确驱动器必须按"失败断言数 != 1"拒绝；只判"编译失败 + EXPECTED + 无 MISMATCH"会漏掉它。
 */

namespace
{
    using namespace ID::Bench::Production;

    inline constexpr auto kCatalog  = std::tuple{ entry_dep(alpha(), kNameAlpha) };
    inline constexpr auto kSelected = std::array<PluginIdentity, 1>{ alpha() };
    using Shape                     = ShapeOf<Plain<decltype(kCatalog)>, 1, 1, 1, 1>;
    inline constexpr auto kPlan     = ID::PluginManager::sort_and_validate(make_input<Shape>(kCatalog, kSelected));

    ID_PRODUCTION_EXPECT_ERROR(kPlan, SelfDependency);

    // 自依赖在建立 active 表之前返回，active_count 实际为 0；这里故意要求 2
    static_assert(kPlan.active_count == 2,
                  "aux_assert_failure: 辅助断言故意失败，用于验证驱动器的失败断言计数判据");
} // namespace
