#include "CaseCommon.hpp"

#include "FixtureAssembly.generated.hpp"

/**
 *  构建证据的链接边与 C++ 推导不一致时，C++ 侧必须拒绝（计划书 8.5 / 16.1）
 *  合法对照：原样生成的证据必须逐项一致。
 *  @note 通过复制证据对象后改掉一条边的上游名字来模拟"链接边被改坏"。
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

    static_assert(kPlan.error == ErrorCode::None, "evidence_edge: 合法对照的计划必须成立");
    static_assert(check_original_evidence() == ErrorCode::None, "evidence_edge: 原样证据必须一致");
#else
    consteval ErrorCode check_tampered_evidence() noexcept
    {
        auto evidence = kEvidence;                                      // 复制生成物
        evidence.edges[0].from = "ID.Fixture.Beta";                     // 边的上游被改坏
        return compare_evidence(kPlan, evidence);
    }

    static_assert(kPlan.error == ErrorCode::None, "evidence_edge: 计划本身必须成立");
    static_assert(check_tampered_evidence() == ErrorCode::EvidenceEdgeMismatch,
                  "ID_ASSEMBLY_ERROR_MISMATCH:EvidenceEdgeMismatch");
    static_assert(check_tampered_evidence() != ErrorCode::EvidenceEdgeMismatch,
                  "ID_ASSEMBLY_ERROR_EXPECTED:EvidenceEdgeMismatch");
#endif
} // namespace
