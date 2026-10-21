#include "ProductionStress.hpp"

/**
 *  根因隔离探针：128/256/32/1 只做计划推导、不给证据。
 *  实测（GCC 16.2.0）：默认 33554432 下本目标超限，失败位置在 bind_entry 的
 *  "revision_in_order 快路径按下标取修订"处（PluginCompileTime.hpp 的 metadata.revisions 读取），
 *  即计划推导（阶段 3 绑定），不是 verify_evidence 的逐记录查找；实测最小可通过档为
 *  37748736（36700160 仍失败）。
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
