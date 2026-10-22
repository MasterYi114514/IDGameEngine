#include "ProductionStress.hpp"

/**
 *  根因隔离探针：128/256/32/1 只做计划推导、不给证据。
 *  本目标保持不加预算参数，继续记录"默认限制档"的失败；A03（kSchemaRevision = 2）之后仍在
 *  默认 33554432 下超限。本次实测的超限点在 bind_active 的候选前缀读取
 *  （PluginCompileTime.hpp 的 declaration.candidate_prefix0[candidate]），即计划推导的阶段 3
 *  匹配绑定，不是 verify_evidence 的逐记录查找。A03 之前记录的超限位置与"最小可通过档
 *  37748736"是在旧 provider[] 扁平表布局下测的，A03 之后未重新二分，不再作为当前结论引用。
 *  与带证据档的差异还有一处夹具结构事实：本目标把输入写成 sort_and_validate(make_input(...))
 *  的一次性实参，输入构造与计划求值落在同一常量表达式里；scale_128x32x1 则先命名一个
 *  constexpr 输入再做计划，二者是两个独立求值。带证据档在默认预算下通过（见 scale_128x32x1）。
 */

namespace
{
    using namespace ID::Bench::Production;
    using namespace ID::Bench::Production::Stress;

    inline constexpr StressProfile kProfile{ 128, 256, 32, 1 };
    inline constexpr auto          kCatalog  = make_catalog<kProfile>(std::make_index_sequence<kProfile.catalog>{});
    inline constexpr auto          kSelected = make_selected<kProfile>(std::make_index_sequence<kProfile.active>{});

    using Shape = AssemblyShape<Plain<decltype(kCatalog)>, 128, 128, 4096, 128, 64>;
    inline constexpr auto kPlan = ID::PluginManager::sort_and_validate(make_input<Shape>(kCatalog, kSelected));

    static_assert(kPlan.diagnostic.code == PluginError::EvidenceSelectionMismatch,
                  "scale_plan_only_1: 空证据必须在验证阶段被拒绝");
    static_assert(kPlan.active_count == 128 && kPlan.edge_count == 127,
                  "scale_plan_only_1: 计划推导必须完成 128 active / 127 边");
} // namespace
