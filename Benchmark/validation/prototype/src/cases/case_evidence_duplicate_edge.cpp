#include "CaseCommon.hpp"

/**
 *  重复的链接边证据：两条相同边不得代替"两个下游各有一条边"（计划书 8.5 / 16.1）
 *  合法对照：同一 TU 用正确的两条边必须逐项一致。
 *  @note 旧实现只检查"边数相等 + 每条边的 (from,to) 能在计划里找到"，
 *        因此 Alpha->Beta 重复两次可以顶替 Alpha->Gamma；现在每条证据必须消费一条
 *        尚未被消费的计划边。
 */

namespace
{
    using namespace ID::Bench::Fixture;
    using namespace ID::Bench::Fixture::Cases;

    inline constexpr char kEdgeAlpha[] = "ID.Case.Edge.Alpha";
    inline constexpr char kEdgeBeta[]  = "ID.Case.Edge.Beta";
    inline constexpr char kEdgeGamma[] = "ID.Case.Edge.Gamma";

    consteval auto make_plan() noexcept
    {
        constexpr auto alpha_decl = declare_dependencies(register_plugin(kEdgeAlpha), std::tuple<>{});
        // 两个下游都只依赖 Alpha：固定图有两条不同边 Alpha->Beta 与 Alpha->Gamma
        constexpr auto beta_decl  = declare_dependencies(register_plugin(kEdgeBeta), std::tuple{ group_of(kEdgeAlpha) });
        constexpr auto gamma_decl = declare_dependencies(register_plugin(kEdgeGamma), std::tuple{ group_of(kEdgeAlpha) });
        constexpr std::array<PluginIdentity, 3> selected{ register_plugin(kEdgeGamma), register_plugin(kEdgeAlpha),
                                                           register_plugin(kEdgeBeta) };
        return sort_and_validate(std::tuple{ alpha_decl, beta_decl, gamma_decl }, selected);
    }

    constexpr auto kPlan = make_plan();

    consteval auto make_evidence() noexcept
    {
        AssemblyEvidence<3, 2, 2> evidence{};
        evidence.selected_count    = 3;
        evidence.selected_names[0] = kEdgeAlpha;
        evidence.selected_names[1] = kEdgeBeta;
        evidence.selected_names[2] = kEdgeGamma;
        evidence.binding_count     = 2;
        evidence.bindings[0]       = { kEdgeBeta, kEdgeAlpha, 0 };
        evidence.bindings[1]       = { kEdgeGamma, kEdgeAlpha, 0 };
        evidence.edge_count        = 2;
        evidence.edges[0]          = { kEdgeAlpha, kEdgeBeta };
        evidence.edges[1]          = { kEdgeAlpha, kEdgeGamma };
        return evidence;
    }

    constexpr auto kEvidence = make_evidence();

#if defined(FIXTURE_CASE_LEGAL)
    consteval ErrorCode check_original_evidence() noexcept
    {
        return compare_evidence(kPlan, kEvidence);
    }

    consteval ErrorCode check_duplicate_probe() noexcept
    {
        auto evidence     = kEvidence;
        evidence.edges[1] = evidence.edges[0];                       // 两条相同边
        return compare_evidence(kPlan, evidence);
    }

    static_assert(kPlan.error == ErrorCode::None, "evidence_duplicate_edge: 合法对照的计划必须成立");
    static_assert(kPlan.edge_count == 2, "evidence_duplicate_edge: 必须正好两条去重边");
    static_assert(check_original_evidence() == ErrorCode::None, "evidence_duplicate_edge: 正确证据必须一致");
    static_assert(check_duplicate_probe() == ErrorCode::EvidenceEdgeMismatch,
                  "evidence_duplicate_edge: 重复边必须被判不一致（否则本用例没有区分力）");
#else
    consteval ErrorCode check_duplicate_evidence() noexcept
    {
        auto evidence     = kEvidence;
        evidence.edges[1] = evidence.edges[0];                       // Alpha->Beta 重复两次，Gamma 的边被顶替
        return compare_evidence(kPlan, evidence);
    }

    static_assert(kPlan.error == ErrorCode::None, "evidence_duplicate_edge: 计划本身必须成立");
    static_assert(check_duplicate_evidence() == ErrorCode::EvidenceEdgeMismatch,
                  "ID_ASSEMBLY_ERROR_MISMATCH:EvidenceEdgeMismatch");
    static_assert(check_duplicate_evidence() != ErrorCode::EvidenceEdgeMismatch,
                  "ID_ASSEMBLY_ERROR_EXPECTED:EvidenceEdgeMismatch");
#endif
} // namespace
