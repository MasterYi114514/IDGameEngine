#include "CaseCommon.hpp"

/**
 *  驱动器自检样本之二（不是被测契约，也不参与 28 条反向用例计数）
 *  本 TU 的输入必然产生 SelfDependency，类别断言写对：输出里会出现 EXPECTED:SelfDependency，
 *  且不会出现 MISMATCH。但同一 TU 里另有一条辅助断言故意失败：自依赖诊断在建立 active 表
 *  之前就返回，active_count 实际为 0，这里却要求 2（GCC 提示 "the comparison reduces to
 *  '(0 == 2)'"），因此编译输出里有两条 "static assertion failed"。
 *  正确行为：RunNegativeCases.cmake 因"失败断言数 != 1"拒绝本目标；
 *  如果驱动器只判"编译失败 + EXPECTED 出现 + 没有 MISMATCH"，本目标会被误判为通过。
 *  @note 该目标由 FixtureNegativeCases 单独构建，并期望驱动器自己的判定结果为 FAIL。
 *        它同时说明：28 条用例的通过不是靠删掉辅助断言换来的——辅助断言单独失败会被抓住。
 *        判据里的失败计数文本来自 GNU 工具链，本机只有 GCC 16.2.0 实测，其它编译器未验证。
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

    ID_FIXTURE_EXPECT_ERROR(kPlan, SelfDependency);        // 类别写对：只产生 EXPECTED，不产生 MISMATCH

    // 辅助断言故意失败：计划里只有 1 个 active，这里要求 2 个
    static_assert(kPlan.active_count == 2,
                  "aux_assert_failure: 辅助断言故意失败，用于验证驱动器的失败断言计数判据");
} // namespace
