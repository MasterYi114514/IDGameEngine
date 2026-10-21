#include "CaseCommon.hpp"

#include "FixtureAssembly.generated.hpp"

/**
 *  构建证据的选中集合与 C++ 推导不一致时，C++ 侧必须拒绝（计划书 8.5 / 16.1）
 *  合法对照：原样生成的证据必须逐项一致。
 *  @note 通过复制证据对象后改掉一个选中名字来模拟"生成物被改坏"，不改动任何生成文件。
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

    static_assert(kPlan.error == ErrorCode::None, "evidence_selection: 合法对照的计划必须成立");
    static_assert(check_original_evidence() == ErrorCode::None, "evidence_selection: 原样证据必须一致");
#else
    consteval ErrorCode check_tampered_evidence() noexcept
    {
        auto evidence = kEvidence;                                      // 复制生成物
        evidence.selected_names[0] = "ID.Fixture.NotSelected";          // 选中集合被改坏
        return compare_evidence(kPlan, evidence);
    }

    static_assert(kPlan.error == ErrorCode::None, "evidence_selection: 计划本身必须成立");
    static_assert(check_tampered_evidence() == ErrorCode::EvidenceSelectionMismatch,
                  "ID_ASSEMBLY_ERROR_MISMATCH:EvidenceSelectionMismatch");
    static_assert(check_tampered_evidence() != ErrorCode::EvidenceSelectionMismatch,
                  "ID_ASSEMBLY_ERROR_EXPECTED:EvidenceSelectionMismatch");
#endif
} // namespace
