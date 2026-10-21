#include "CaseCommon.hpp"

/**
 *  证据计数越界：证据里声明的条数大于数组容量时，必须**在索引之前**返回不一致类别（计划书 8.5 / 16.1）
 *  合法对照：容量足够的证据必须逐项一致（证明下面的失败只来自容量越界，不是别的原因）。
 *  @note 三类证据各自构造一个"计数 == 计划实际条数、但大于证据数组容量"的对象：
 *        选中集合 3 条 / 容量 2；逐需求绑定 2 条 / 容量 1；链接边 2 条 / 容量 1。
 *        旧实现会直接按下标读数组（常量求值里就是越界访问），现在只返回类别。
 */

namespace
{
    using namespace ID::Bench::Fixture;
    using namespace ID::Bench::Fixture::Cases;

    inline constexpr char kCapAlpha[] = "ID.Case.Cap.Alpha";
    inline constexpr char kCapBeta[]  = "ID.Case.Cap.Beta";
    inline constexpr char kCapGamma[] = "ID.Case.Cap.Gamma";

    consteval auto make_plan() noexcept
    {
        constexpr auto alpha_decl = declare_dependencies(register_plugin(kCapAlpha), std::tuple<>{});
        constexpr auto gamma_decl = declare_dependencies(register_plugin(kCapGamma), std::tuple<>{});
        // Beta 两个需求各一个已选候选：2 条绑定、2 条去重边
        constexpr auto beta_decl = declare_dependencies(register_plugin(kCapBeta),
                                                        std::tuple{ group_of(kCapAlpha), group_of(kCapGamma) });
        constexpr std::array<PluginIdentity, 3> selected{ register_plugin(kCapBeta), register_plugin(kCapGamma),
                                                          register_plugin(kCapAlpha) };
        return sort_and_validate(std::tuple{ alpha_decl, beta_decl, gamma_decl }, selected);
    }

    constexpr auto kPlan = make_plan();

    // 容量充足：3 条选中 / 2 条绑定 / 2 条边
    consteval auto make_full_evidence() noexcept
    {
        AssemblyEvidence<3, 2, 2> evidence{};
        evidence.selected_count    = 3;
        evidence.selected_names[0] = kCapAlpha;
        evidence.selected_names[1] = kCapBeta;
        evidence.selected_names[2] = kCapGamma;
        evidence.binding_count     = 2;
        evidence.bindings[0]       = { kCapBeta, kCapAlpha, 0 };
        evidence.bindings[1]       = { kCapBeta, kCapGamma, 1 };
        evidence.edge_count        = 2;
        evidence.edges[0]          = { kCapAlpha, kCapBeta };
        evidence.edges[1]          = { kCapGamma, kCapBeta };
        return evidence;
    }

    // 选中集合容量 2，但计数 3（等于计划 active 数）：只有容量检查能拦住
    consteval auto make_short_selection_evidence() noexcept
    {
        AssemblyEvidence<2, 2, 2> evidence{};
        evidence.selected_count    = 3;
        evidence.selected_names[0] = kCapAlpha;
        evidence.selected_names[1] = kCapBeta;
        return evidence;
    }

    // 绑定容量 1，但计数 2（等于计划绑定数且选中集合已通过）
    consteval auto make_short_binding_evidence() noexcept
    {
        AssemblyEvidence<3, 1, 2> evidence{};
        evidence.selected_count    = 3;
        evidence.selected_names[0] = kCapAlpha;
        evidence.selected_names[1] = kCapBeta;
        evidence.selected_names[2] = kCapGamma;
        evidence.binding_count     = 2;
        evidence.bindings[0]       = { kCapBeta, kCapAlpha, 0 };
        return evidence;
    }

    // 边容量 1，但计数 2（选中集合与绑定都已通过）
    consteval auto make_short_edge_evidence() noexcept
    {
        AssemblyEvidence<3, 2, 1> evidence{};
        evidence.selected_count    = 3;
        evidence.selected_names[0] = kCapAlpha;
        evidence.selected_names[1] = kCapBeta;
        evidence.selected_names[2] = kCapGamma;
        evidence.binding_count     = 2;
        evidence.bindings[0]       = { kCapBeta, kCapAlpha, 0 };
        evidence.bindings[1]       = { kCapBeta, kCapGamma, 1 };
        evidence.edge_count        = 2;
        evidence.edges[0]          = { kCapAlpha, kCapBeta };
        return evidence;
    }

#if defined(FIXTURE_CASE_LEGAL)
    static_assert(kPlan.error == ErrorCode::None, "evidence_capacity: 合法对照的计划必须成立");
    static_assert(kPlan.active_count == 3 && kPlan.edge_count == 2, "evidence_capacity: 计划规模必须符合夹具设计");
    static_assert(compare_evidence(kPlan, make_full_evidence()) == ErrorCode::None,
                  "evidence_capacity: 容量充足的正确证据必须一致（证明下面的失败只来自容量越界）");
    static_assert(compare_evidence(kPlan, make_short_selection_evidence()) == ErrorCode::EvidenceSelectionMismatch,
                  "evidence_capacity: 选中计数越界必须被判不一致（合法对照侧的区分力证明）");
    static_assert(compare_evidence(kPlan, make_short_binding_evidence()) == ErrorCode::EvidenceProviderMismatch,
                  "evidence_capacity: 绑定计数越界必须被判不一致（合法对照侧的区分力证明）");
    static_assert(compare_evidence(kPlan, make_short_edge_evidence()) == ErrorCode::EvidenceEdgeMismatch,
                  "evidence_capacity: 边计数越界必须被判不一致（合法对照侧的区分力证明）");
#else
    static_assert(kPlan.error == ErrorCode::None, "evidence_capacity: 计划本身必须成立");
    static_assert(compare_evidence(kPlan, make_short_selection_evidence()) == ErrorCode::EvidenceSelectionMismatch,
                  "ID_ASSEMBLY_ERROR_MISMATCH:EvidenceSelectionMismatch");
    static_assert(compare_evidence(kPlan, make_short_selection_evidence()) != ErrorCode::EvidenceSelectionMismatch,
                  "ID_ASSEMBLY_ERROR_EXPECTED:EvidenceSelectionMismatch");
#endif
} // namespace
