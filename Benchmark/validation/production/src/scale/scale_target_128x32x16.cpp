#include "ProductionStress.hpp"

/**
 *  有限预算压力目标（GNU 限定）：128 active / 256 catalog / 32 需求 / 每组 16 候选
 *  仅本目标追加 -fconstexpr-ops-limit=184549376；该值是 GCC 16.2.0 上实测的最小可通过档：
 *  默认 33554432 在夹具构造就超限、134217728 与 183500800 在生产校验超限、184549376 通过。
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
    static_assert(kPlan.provider[127 * 32 + 15] == kPlan.find_active("ID.Case.N0"),
                  "scale_target_128x32x16: 第 16 个候选命中后仍只能绑定 P0");
    static_assert(kPlan.provision_count == 128, "scale_target_128x32x16: provides 记录数必须为 128");
    static_assert(kPlan.fingerprint != 0, "scale_target_128x32x16: 必须输出确定性指纹");
} // namespace
