#include "ProductionStress.hpp"

/**
 *  根因隔离探针（不是"合法产品"目标）：128/256/32/16 的**计划推导**单独执行，
 *  故意不给构建证据，因此最终错误必须是 EvidenceSelectionMismatch。
 *  1. 实测（GCC 16.2.0）：默认 33554432 下本目标连输入都构造不出来——
 *     失败在夹具 make_catalog 的候选循环（ProductionStress.hpp 的 groups 写入），
 *     而不是 verify_evidence 或图算法；因此本档不能用来证明"默认预算的瓶颈在证据校验"；
 *  2. 本目标不进入合法对照，也不宣称生产满组合已通过；
 *  3. 注意本目标把输入写成 sort_and_validate(make_input(...)) 的一次性实参，
 *     输入构造与计划求值在同一常量表达式内；带证据档则先命名 constexpr 输入。
 */

namespace
{
    using namespace ID::Bench::Production;
    using namespace ID::Bench::Production::Stress;

    inline constexpr StressProfile kProfile{ 128, 256, 32, 16 };
    inline constexpr auto          kCatalog  = make_catalog<kProfile>(std::make_index_sequence<kProfile.catalog>{});
    inline constexpr auto          kSelected = make_selected<kProfile>(std::make_index_sequence<kProfile.active>{});

    using Shape = AssemblyShape<Plain<decltype(kCatalog)>, 128, 128, 4096, 128, 64>;
    inline constexpr auto kPlan = ID::PluginManager::sort_and_validate(make_input<Shape>(kCatalog, kSelected));

    static_assert(kPlan.diagnostic.code == PluginError::EvidenceSelectionMismatch,
                  "scale_plan_only: 空证据必须在验证阶段被拒绝，证明计划推导已完成");
    static_assert(kPlan.active_count == 128 && kPlan.edge_count == 127,
                  "scale_plan_only: 计划推导必须产生 128 active / 127 去重边");
} // namespace
