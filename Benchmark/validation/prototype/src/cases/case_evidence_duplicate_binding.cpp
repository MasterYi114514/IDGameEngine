#include "CaseCommon.hpp"

/**
 *  重复的逐需求 provider 证据：两条相同记录不得代替"两个不同需求各绑一个上游"（计划书 8.5 / 16.1）
 *  合法对照：同一 TU 用正确的两条记录必须逐项一致。
 *  @note 旧实现只检查"条数相等 + 每条记录能在计划里找到同名 provider"，
 *        因此 (Beta, req0 -> Alpha) 重复两次可以顶替 (Beta, req1 -> Gamma)；
 *        现在每条记录必须消费一个不同的 (active, requirement) 槽。
 */

namespace
{
    using namespace ID::Bench::Fixture;
    using namespace ID::Bench::Fixture::Cases;

    inline constexpr char kBindAlpha[] = "ID.Case.Bind.Alpha";
    inline constexpr char kBindBeta[]  = "ID.Case.Bind.Beta";
    inline constexpr char kBindGamma[] = "ID.Case.Bind.Gamma";
    inline constexpr char kBindDelta[] = "ID.Case.Bind.Delta";

    consteval auto make_plan() noexcept
    {
        constexpr auto alpha_decl = declare_dependencies(register_plugin(kBindAlpha), std::tuple<>{});
        constexpr auto gamma_decl = declare_dependencies(register_plugin(kBindGamma), std::tuple<>{});
        constexpr auto delta_decl = declare_dependencies(register_plugin(kBindDelta), std::tuple<>{});
        // Beta 的两个需求各只有一个已选候选：req0 -> Alpha，req1 -> Gamma
        constexpr auto beta_decl = declare_dependencies(register_plugin(kBindBeta),
                                                        std::tuple{ group_of(kBindAlpha), group_of(kBindGamma) });
        constexpr std::array<PluginIdentity, 4> selected{ register_plugin(kBindBeta), register_plugin(kBindDelta),
                                                          register_plugin(kBindGamma),
                                                          register_plugin(kBindAlpha) };
        return sort_and_validate(std::tuple{ alpha_decl, beta_decl, gamma_decl, delta_decl }, selected);
    }

    constexpr auto kPlan = make_plan();

    // 正确的两条绑定记录（Beta 的两个需求分别绑 Alpha / Gamma）
    consteval auto make_evidence() noexcept
    {
        AssemblyEvidence<4, 2, 2> evidence{};
        evidence.selected_count    = 4;
        evidence.selected_names[0] = kBindAlpha;
        evidence.selected_names[1] = kBindBeta;
        evidence.selected_names[2] = kBindGamma;
        evidence.selected_names[3] = kBindDelta;
        evidence.binding_count     = 2;
        evidence.bindings[0]       = { kBindBeta, kBindAlpha, 0 };
        evidence.bindings[1]       = { kBindBeta, kBindGamma, 1 };
        evidence.edge_count        = 2;
        evidence.edges[0]          = { kBindAlpha, kBindBeta };
        evidence.edges[1]          = { kBindGamma, kBindBeta };
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
        auto evidence        = kEvidence;
        evidence.bindings[1] = evidence.bindings[0];                 // 两条相同记录
        return compare_evidence(kPlan, evidence);
    }

    static_assert(kPlan.error == ErrorCode::None, "evidence_duplicate_binding: 合法对照的计划必须成立");
    static_assert(kPlan.active_requirement[find_active_index(kPlan, kBindBeta)] == 2,
                  "evidence_duplicate_binding: Beta 必须有两个需求组");
    static_assert(check_original_evidence() == ErrorCode::None, "evidence_duplicate_binding: 正确证据必须一致");
    static_assert(check_duplicate_probe() == ErrorCode::EvidenceProviderMismatch,
                  "evidence_duplicate_binding: 重复记录必须被判不一致（否则本用例没有区分力）");
#else
    consteval ErrorCode check_duplicate_evidence() noexcept
    {
        auto evidence        = kEvidence;
        evidence.bindings[1] = evidence.bindings[0];                 // (Beta, req0 -> Alpha) 重复两次
        return compare_evidence(kPlan, evidence);
    }

    static_assert(kPlan.error == ErrorCode::None, "evidence_duplicate_binding: 计划本身必须成立");
    static_assert(check_duplicate_evidence() == ErrorCode::EvidenceProviderMismatch,
                  "ID_ASSEMBLY_ERROR_MISMATCH:EvidenceProviderMismatch");
    static_assert(check_duplicate_evidence() != ErrorCode::EvidenceProviderMismatch,
                  "ID_ASSEMBLY_ERROR_EXPECTED:EvidenceProviderMismatch");
#endif
} // namespace
