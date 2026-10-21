#include <array>
#include <tuple>

#include "FixtureAlphaApi.hpp"
#include "FixtureAssembly.generated.hpp"
#include "FixtureBetaApi.hpp"
#include "FixtureModules.hpp"

/**
 *  编译期组装计划（P0）
 *  1. catalog 与选中集合全部由 CMake 从 plugin.json + profile 生成，本 TU 不手写第二份 manifest；
 *  2. 三个 consteval 操作在这里完成绑定、建图、拓扑排序；
 *  3. 用生成证据交叉校验，并把"上游先于下游""逐需求唯一 provider""去重边"固化成 static_assert，
 *     因此这个 TU 能否编译本身就是一项校验（计划书 9.1 的验证 TU 作用）。
 */

namespace
{
    using namespace ID::Bench::Fixture;

    constexpr auto kCatalog = Generated::make_catalog();
    constexpr auto kPlan    = sort_and_validate(kCatalog, Generated::kSelected);

    static_assert(std::tuple_size_v<decltype(kCatalog)> == Generated::kCatalogCount,
                  "catalog 条目数必须与清单数量一致");
    // 正向校验：这里失败说明"产品选择本身不合法"，不是反向用例的期望失败
    static_assert(kPlan.error == ErrorCode::None, "fixture 产品的组装计划必须合法");
    static_assert(kPlan.active_count == Generated::kSelectedCount, "active 数必须等于选中集合大小");
    static_assert(compare_evidence(kPlan, Generated::kEvidence) == ErrorCode::None,
                  "构建证据必须与 C++ 推导逐项一致");

    constexpr uint16_t kAlphaIndex = find_active_index(kPlan, kAlphaPluginName);
    constexpr uint16_t kBetaIndex  = find_active_index(kPlan, kBetaPluginName);

    static_assert(kAlphaIndex != kNoIndex && kBetaIndex != kNoIndex, "两个 fixture 模块都必须出现在计划里");
    static_assert(kPlan.topo_order[0] == kAlphaIndex, "拓扑序首个必须是上游 Alpha");
    static_assert(kPlan.topo_order[1] == kBetaIndex, "拓扑序第二必须是下游 Beta");
    static_assert(kPlan.provider[static_cast<size_t>(kBetaIndex) * kMaxRequirements] == kAlphaIndex,
                  "Beta 的第 0 个需求必须唯一绑定到 Alpha");
    static_assert(kPlan.edge_count == Generated::kEdgeCount, "去重边数必须与构建证据一致");
    static_assert(kPlan.edge_begin[kBetaIndex] + 1 == kPlan.edge_begin[kBetaIndex + 1], "Beta 只应有一条入边");
    static_assert(kPlan.edge_target[kPlan.edge_begin[kBetaIndex]] == kAlphaIndex, "Beta 的入边必须来自 Alpha");
    static_assert(kPlan.fingerprint != 0, "确定性指纹必须被计算");
} // namespace
