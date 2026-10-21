#include "CaseCommon.hpp"

/**
 *  每组候选数上限：一个需求组最多 16 个候选（计划书 6.5 / 7）
 *  合法对照：同一组 16 个候选，只选中其中一个 → 组装成功。
 *  非法：17 个候选 → 必须在任何结果索引写入之前诊断为 TooManyCandidates。
 */

namespace
{
    using namespace ID::Bench::Fixture;
    using namespace ID::Bench::Fixture::Cases;

    // 17 个互不相同的候选名：合法对照只用到前 16 个
    // 每行恰好 13 字符 + NUL：register_plugin 以数组长度推导名字长度，行宽必须精确
    inline constexpr char kDepNames[17][14] = {
        "ID.Case.Dep00", "ID.Case.Dep01", "ID.Case.Dep02", "ID.Case.Dep03", "ID.Case.Dep04",
        "ID.Case.Dep05", "ID.Case.Dep06", "ID.Case.Dep07", "ID.Case.Dep08", "ID.Case.Dep09",
        "ID.Case.Dep10", "ID.Case.Dep11", "ID.Case.Dep12", "ID.Case.Dep13", "ID.Case.Dep14",
        "ID.Case.Dep15", "ID.Case.Dep16"
    };

    template <size_t Count>
    consteval std::array<std::string_view, Count> candidate_group() noexcept
    {
        std::array<std::string_view, Count> names{};
        for (size_t i = 0; i < Count; ++i)
        {
            names[i] = std::string_view(kDepNames[i]);
        }
        return names;
    }

    template <size_t... Index>
    consteval auto make_case_plan(std::index_sequence<Index...>) noexcept
    {
#if defined(FIXTURE_CASE_LEGAL)
        constexpr size_t kCandidates = kMaxCandidatesPerGroup;          // 16：合法上限
#else
        constexpr size_t kCandidates = kMaxCandidatesPerGroup + 1;      // 17：必须拒绝
#endif
        constexpr auto owner = declare_dependencies(register_plugin("ID.Case.Owner"),
                                                    std::tuple{ candidate_group<kCandidates>() });
        // catalog 里补齐 17 个候选名（每个都只由 Owner 依赖，自身无上游）
        constexpr std::array<PluginIdentity, 2> selected{ register_plugin("ID.Case.Owner"),
                                                          register_plugin(kDepNames[0]) };
        return sort_and_validate(
            std::tuple{ owner, declare_dependencies(register_plugin(kDepNames[Index]), std::tuple<>{})... },
            selected);
    }

    constexpr auto kPlan = make_case_plan(std::make_index_sequence<17>{});

#if defined(FIXTURE_CASE_LEGAL)
    static_assert(kPlan.error == ErrorCode::None, "too_many_candidates: 16 个候选必须合法");
    static_assert(kPlan.provider[static_cast<size_t>(find_active_index(kPlan, "ID.Case.Owner")) * kMaxRequirements] ==
                      find_active_index(kPlan, kDepNames[0]),
                  "too_many_candidates: 唯一被选中的候选必须成为 provider");
#else
    ID_FIXTURE_EXPECT_ERROR(kPlan, TooManyCandidates);
#endif
} // namespace
