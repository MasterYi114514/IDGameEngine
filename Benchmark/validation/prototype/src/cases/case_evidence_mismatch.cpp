#include "CaseCommon.hpp"

#include "FixtureAssembly.generated.hpp"

/**
 *  构建证据被改坏：CMake 派生证据与 C++ 重新推导不一致时，C++ 侧必须拒绝（计划书 8.5 / 16.1）
 *  合法对照：同一 TU 使用原样生成的证据，必须逐项一致。
 *  @note 这里通过复制证据对象后篡改一个 provider 名字来模拟"生成物被改坏"，
 *        不改动任何生成文件本身。
 */

namespace
{
    using namespace ID::Bench::Fixture;
    using namespace ID::Bench::Fixture::Cases;
    using namespace ID::Bench::Fixture::Generated;

    constexpr auto kPlan = sort_and_validate(make_catalog(), kSelected);

#if defined(FIXTURE_CASE_LEGAL)
    consteval ErrorCode check_original_evidence() noexcept
    {
        return compare_evidence(kPlan, kEvidence);
    }

    static_assert(kPlan.error == ErrorCode::None, "evidence_mismatch: 合法对照的计划必须成立");
    static_assert(check_original_evidence() == ErrorCode::None, "evidence_mismatch: 原样证据必须一致");
#else
    consteval ErrorCode check_tampered_evidence() noexcept
    {
        auto evidence = kEvidence;                                    // 复制 CMake 生成的证据
        evidence.bindings[0].provider = kNameBeta;                    // 把 provider 改成另一个插件名
        return compare_evidence(kPlan, evidence);
    }

    static_assert(kPlan.error == ErrorCode::None, "evidence_mismatch: 计划本身必须成立");
    // 与 error_matches/error_differs 相同的互斥形态，只是被断言的值是 compare_evidence 的返回值
    static_assert(check_tampered_evidence() == ErrorCode::EvidenceProviderMismatch,
                  "ID_ASSEMBLY_ERROR_MISMATCH:EvidenceProviderMismatch");
    static_assert(check_tampered_evidence() != ErrorCode::EvidenceProviderMismatch,
                  "ID_ASSEMBLY_ERROR_EXPECTED:EvidenceProviderMismatch");
#endif
} // namespace
