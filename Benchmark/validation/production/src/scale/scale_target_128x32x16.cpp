#include "ProductionStress.hpp"

/**
 *  有限预算压力目标（GNU 限定）：128 active / 256 catalog / 32 需求 / 每组 16 候选
 *  A03（kSchemaRevision = 2）之后本档追加 -fconstexpr-ops-limit=192413696，与 128x32x2/4/8 候选档
 *  共用 CMake 变量 IDP_SCALE_CONSTEXPR_OPS_LIMIT；本次实二分：189792256 失败、192413696 通过。
 *  该值随 A03 的匹配静态计划表上移（match_begin 4097 项 + match_active 65536 项 + edge_target 16256 项
 *  ≈ 180 KiB / 每个结果对象）：A03 之前实测最小通过档 184549376 现在也超限。
 *  普通生产目标与普通 Benchmark 目标都不加该参数，也不进全局 CMAKE_CXX_FLAGS。
 *  MSVC / clang 未验证，不创建对应目标。
 */

#if !defined(__GNUC__) || defined(__clang__)
#error "scale_target_128x32x16: 有限常量求值预算只在 GCC 16.2.0 实测；其它编译器不得据此宣称通过"
#endif

namespace
{
    using namespace ID::Bench::Production;
    using namespace ID::Bench::Production::Stress;

    inline constexpr StressProfile kProfile{ 128, 256, 32, 16 };
    inline constexpr auto          kCatalog  = make_catalog<kProfile>(std::make_index_sequence<kProfile.catalog>{});
    inline constexpr auto          kSelected = make_selected<kProfile>(std::make_index_sequence<kProfile.active>{});

    using Shape = AssemblyShape<Plain<decltype(kCatalog)>, 128, 128, 4096, 128, 64>;
    inline constexpr auto kInput = make_input_with_evidence<kProfile, Shape>(kCatalog, kSelected);
    static_assert(evidence_complete<kProfile, Shape>(kInput), "scale_target_128x32x16: 证据记录条数必须完整");

    inline constexpr auto kPlan = ID::PluginManager::sort_and_validate(kInput);

    static_assert(kPlan.diagnostic.code == PluginError::None, "scale_target_128x32x16: 有限预算下必须合法");
    static_assert(kPlan.active_count == 128, "scale_target_128x32x16: active 必须为 128");
    static_assert(kPlan.edge_count == 127, "scale_target_128x32x16: 去重边只能有 127 条");
    static_assert(kPlan.active_requirement_count[127] == 32,
                  "scale_target_128x32x16: 最后一个 active 必须真实绑定 32 条需求");
    static_assert(matched_exactly(kPlan, 127, 15, kPlan.find_active("ID.Case.N0")),
                  "scale_target_128x32x16: 16 候选组里只有被选中的 P0 计入命中，第 15 组仍恰好 1 条");
    static_assert(kPlan.matched_total() == 127 * kMaxRequirements,
                  "scale_target_128x32x16: 命中记录总数必须等于 127 个下游 × 32 需求（未选备选不计命中）");
    static_assert(kPlan.provision_count == 128, "scale_target_128x32x16: provides 记录数必须为 128");
    static_assert(kPlan.fingerprint != 0, "scale_target_128x32x16: 必须输出确定性指纹");
} // namespace
