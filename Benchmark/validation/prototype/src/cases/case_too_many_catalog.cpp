#include "CaseCommon.hpp"

#include "FixtureStressSupport.hpp"

/**
 *  catalog 容量：最多 256 条声明（计划书 6.5 / 7）
 *  合法对照：256 条（只用 catalog 条目，不要求都选中）。
 *  非法：257 条 → 必须诊断为 TooManyCatalog，而不是越界写 catalog 索引表。
 *  @note 诊断来自 sort_and_validate 的 `if constexpr (kCatalogSize > kMaxCatalog)` 提前返回：
 *        第 257 条 TU 会把 257 个声明都实例化出来（类型层面确实超限），但收集阶段的
 *        `catalog_count >= kMaxCatalog` 守卫在这条输入形状下**不会被执行**——它只是
 *        "声明元组长度被绕过"时的防御性护栏。诊断类别相同，但不要把提前返回说成"收集第 257 条时报错"。
 */

namespace
{
    using namespace ID::Bench::Fixture;
    using namespace ID::Bench::Fixture::Cases;

    /**
     *  @brief 第 Index 条声明：前 256 条用压力名字池，第 257 条用字面量名
     *  @note 全部是"无上游"的声明，因此本用例只测 catalog 容量，不引入依赖/绑定噪声
     */
    template <size_t Index>
    consteval auto make_entry() noexcept
    {
        if constexpr (Index < kMaxCatalog)
        {
            return declare_dependencies(Stress::pool_identity(Index), std::tuple<>{});
        }
        else
        {
            return declare_dependencies(register_plugin("ID.Case.Overflow"), std::tuple<>{});
        }
    }

    template <size_t... Index>
    consteval auto make_case_plan(std::index_sequence<Index...>) noexcept
    {
        constexpr std::array<PluginIdentity, 1> selected{ Stress::pool_identity(0) };
        return sort_and_validate(std::tuple{ make_entry<Index>()... }, selected);
    }

#if defined(FIXTURE_CASE_LEGAL)
    constexpr auto kPlan = make_case_plan(std::make_index_sequence<kMaxCatalog>{});
#else
    constexpr auto kPlan = make_case_plan(std::make_index_sequence<kMaxCatalog + 1>{});
#endif

#if defined(FIXTURE_CASE_LEGAL)
    static_assert(kPlan.error == ErrorCode::None, "too_many_catalog: 256 条必须合法");
    static_assert(kPlan.active_count == 1, "too_many_catalog: 只选中 1 个 active");
#else
    ID_FIXTURE_EXPECT_ERROR(kPlan, TooManyCatalog);
#endif
} // namespace
