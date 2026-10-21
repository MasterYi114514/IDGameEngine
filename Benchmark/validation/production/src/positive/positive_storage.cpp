#include "ProductionCaseSupport.hpp"
#include "ProductionPositiveProbes.hpp"

/**
 *  正向对照：拥有存储。复制结果 / 声明 / catalog 条目后，名字与能力 view 必须仍指向副本自身，
 *  不能悬空到常量求值期的临时对象。
 *  编译期断言 + 运行期探针：TU 必须进入 ProductionCaseControls 的链接行并能被调用。
 */

namespace
{
    using namespace ID::Bench::Production;

    inline constexpr auto kCatalog  = baseline_catalog();
    inline constexpr auto kSelected = baseline_selected();
    using Shape                     = ShapeOf<Plain<decltype(kCatalog)>, 2, 2, 1, 1>;
    inline constexpr auto kPlan     = ID::PluginManager::sort_and_validate(make_evidence_input<Shape>(kCatalog, kSelected));

    static_assert(kPlan.diagnostic.code == PluginError::None, "positive_storage: 基线必须合法");

    // ---- 复制计划结果后重新读取名字与能力 view ----
    inline constexpr auto kPlanCopy = kPlan;

    static_assert(kPlanCopy.active_identity[0].view() == "ID.Case.Alpha", "positive_storage: 结果副本名字不得悬空");
    static_assert(kPlanCopy.provision_capability_view(0) == "ID.Case.CapA",
                  "positive_storage: 结果副本能力名不得悬空");
    static_assert(kPlanCopy.find_active("ID.Case.Beta") == 1, "positive_storage: 结果副本 find_active 必须可用");

    // ---- 复制声明后候选名与身份必须仍可读 ----
    consteval bool declaration_copy_ok() noexcept
    {
        const auto declaration = entry_dep(alpha(), kNameBeta).declaration;
        const auto copy        = declaration;
        return copy.identity.view() == "ID.Case.Alpha" && copy.candidate_view(0) == "ID.Case.Beta" &&
               copy.candidate_count == 1 && copy.group_count == 1;
    }

    static_assert(declaration_copy_ok(), "positive_storage: 声明副本的候选名与身份不得悬空");

    // ---- 复制 catalog 条目后元数据 view 必须仍可读 ----
    consteval bool entry_copy_ok() noexcept
    {
        const auto entry = entry_leaf(beta());
        const auto copy  = entry;
        return copy.declaration.identity.view() == "ID.Case.Beta" && copy.metadata.target == kTargetBeta &&
               copy.metadata.provides[0].capability == kCapBeta;
    }

    static_assert(entry_copy_ok(), "positive_storage: catalog 条目副本的元数据不得悬空");
} // namespace

namespace ID::Bench::Production::Positive
{
    ProbeResult storage() noexcept
    {
        using namespace ID::Bench::Production;
        return ProbeResult{ kPlanCopy.active_count, kPlanCopy.edge_count, kPlanCopy.provision_count,
                            static_cast<uint8_t>(kPlanCopy.diagnostic.code) };
    }
} // namespace ID::Bench::Production::Positive
