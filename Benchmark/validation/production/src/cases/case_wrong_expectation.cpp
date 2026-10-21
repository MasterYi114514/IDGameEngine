#include "ProductionCaseSupport.hpp"

/**
 *  驱动器自检样本（不是被测契约，不计入反向用例数）
 *  输入必然产生 SelfDependency，却故意把期望类别写成 DuplicatePlugin：
 *  正确的驱动器必须按 MISMATCH 拒绝，否则说明它只看"编译失败"不看类别。
 */

namespace
{
    using namespace ID::Bench::Production;

    inline constexpr auto kCatalog  = std::tuple{ entry_dep(alpha(), kNameAlpha) };
    inline constexpr auto kSelected = std::array<PluginIdentity, 1>{ alpha() };
    using Shape                     = ShapeOf<Plain<decltype(kCatalog)>, 1, 1, 1, 1>;
    inline constexpr auto kPlan     = ID::PluginManager::sort_and_validate(make_input<Shape>(kCatalog, kSelected));

    ID_PRODUCTION_EXPECT_ERROR(kPlan, DuplicatePlugin);      // 实际是 SelfDependency
} // namespace
