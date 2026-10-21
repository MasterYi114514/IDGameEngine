#include "CaseCommon.hpp"

/**
 *  驱动器自检样本（不是被测契约，也不参与"28 条反向用例"计数）
 *  本 TU 的输入必然产生 SelfDependency，却故意把期望类别写成 DuplicatePlugin：
 *  正确的驱动器必须把它判为"类别不符"并拒绝，否则说明驱动器只看"编译失败"而不看类别。
 *  @note 该目标由 RunNegativeCases.cmake 单独构建，并期望驱动器自己的判定结果为 FAIL。
 */

namespace
{
    using namespace ID::Bench::Fixture;
    using namespace ID::Bench::Fixture::Cases;

    consteval auto make_case_plan() noexcept
    {
        constexpr auto declaration_alpha = declare_dependencies(alpha(), std::tuple{ group_of(kNameAlpha) });
        constexpr std::array<PluginIdentity, 1> selected{ alpha() };
        return sort_and_validate(std::tuple{ declaration_alpha }, selected);
    }

    constexpr auto kPlan = make_case_plan();

    // 故意写错期望类别：实际是 SelfDependency
    ID_FIXTURE_EXPECT_ERROR(kPlan, DuplicatePlugin);
} // namespace
