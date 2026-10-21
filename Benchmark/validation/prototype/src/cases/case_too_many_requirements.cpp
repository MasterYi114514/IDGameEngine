#include "CaseCommon.hpp"

/**
 *  需求组数上限：每插件最多 32 个需求组（计划书 6.5 / 7）
 *  合法对照：同一插件改成 32 组（每组 1 个已选候选），组装必须成功。
 *  @note 第 33 组必须在**任何结果索引写入之前**被诊断成 TooManyRequirements：
 *        provider 行宽固定为 kMaxRequirements，先写后查会越界或静默错绑到下一个插件。
 */

namespace
{
    using namespace ID::Bench::Fixture;
    using namespace ID::Bench::Fixture::Cases;

    template <size_t... Index>
    consteval auto make_groups(std::index_sequence<Index...>) noexcept
    {
        return std::tuple{ (static_cast<void>(Index), group_of(kNameAlpha))... };
    }

    consteval auto make_case_plan() noexcept
    {
#if defined(FIXTURE_CASE_LEGAL)
        constexpr size_t kGroups = kMaxRequirements;          // 32：合法上限
#else
        constexpr size_t kGroups = kMaxRequirements + 1;      // 33：必须拒绝
#endif
        constexpr auto declaration_alpha = declare_dependencies(alpha(), std::tuple<>{});
        constexpr auto declaration_gamma =
            declare_dependencies(gamma(), make_groups(std::make_index_sequence<kGroups>{}));
        constexpr std::array<PluginIdentity, 2> selected{ alpha(), gamma() };
        return sort_and_validate(std::tuple{ declaration_alpha, declaration_gamma }, selected);
    }

    constexpr auto kPlan = make_case_plan();

#if defined(FIXTURE_CASE_LEGAL)
    static_assert(kPlan.error == ErrorCode::None, "too_many_requirements: 32 组必须合法");
    static_assert(kPlan.edge_count == 1, "too_many_requirements: 32 组同一上游必须只留一条边");
#else
    ID_FIXTURE_EXPECT_ERROR(kPlan, TooManyRequirements);
#endif
} // namespace
