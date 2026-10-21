#include "CaseCommon.hpp"

#include "FixtureAssembly.generated.hpp"

/**
 *  重复的选中集合证据：证据写成 {Alpha, Alpha} 不得被当成 {Alpha, Beta}（计划书 8.5 / 16.1）
 *  合法对照：同一 TU 使用原样生成的证据，必须逐项一致。
 *  @note 旧实现只做"每条名字都能在计划里找到 + 数量相等"，因此用重复名字替换缺失名字可以蒙混过关；
 *        现在每条证据必须消费一个**尚未被消费**的 active 下标，重复即判 EvidenceSelectionMismatch。
 */

namespace
{
    using namespace ID::Bench::Fixture;
    using namespace ID::Bench::Fixture::Generated;

    constexpr auto kPlan = sort_and_validate(make_catalog(), kSelected);

#if defined(FIXTURE_CASE_LEGAL)
    consteval ErrorCode check_original_evidence() noexcept
    {
        return compare_evidence(kPlan, kEvidence);
    }

    // 同一 TU 里的区分力证明：把证据改成重复名字后必须被判不一致
    consteval ErrorCode check_duplicate_probe() noexcept
    {
        auto evidence = kEvidence;
        evidence.selected_names[1] = evidence.selected_names[0];        // {Alpha, Alpha}
        return compare_evidence(kPlan, evidence);
    }

    static_assert(kPlan.error == ErrorCode::None, "evidence_duplicate_selection: 合法对照的计划必须成立");
    static_assert(kPlan.active_count == 2, "evidence_duplicate_selection: 合法对照必须有 2 个 active");
    static_assert(check_original_evidence() == ErrorCode::None, "evidence_duplicate_selection: 原样证据必须一致");
    static_assert(check_duplicate_probe() == ErrorCode::EvidenceSelectionMismatch,
                  "evidence_duplicate_selection: 重复名字必须被判不一致（否则本用例没有区分力）");
#else
    consteval ErrorCode check_duplicate_evidence() noexcept
    {
        auto evidence = kEvidence;                                      // 复制 CMake 生成的证据
        evidence.selected_names[1] = evidence.selected_names[0];        // {Alpha, Alpha} 代替 {Alpha, Beta}
        return compare_evidence(kPlan, evidence);
    }

    static_assert(kPlan.error == ErrorCode::None, "evidence_duplicate_selection: 计划本身必须成立");
    static_assert(check_duplicate_evidence() == ErrorCode::EvidenceSelectionMismatch,
                  "ID_ASSEMBLY_ERROR_MISMATCH:EvidenceSelectionMismatch");
    static_assert(check_duplicate_evidence() != ErrorCode::EvidenceSelectionMismatch,
                  "ID_ASSEMBLY_ERROR_EXPECTED:EvidenceSelectionMismatch");
#endif
} // namespace
