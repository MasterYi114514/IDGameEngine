#include "CaseCommon.hpp"

/**
 *  长环 + 环下游残余节点：必须记录**完整真实环路径**，并把残余节点排除在外（计划书 6.3 / 7）
 *  合法对照：把 Ring.0 的上游去掉，同一组节点变成合法 DAG，一条边都不少地完成拓扑排序。
 *  @note 旧实现把环成员记录上限写死为 8，9 节点环会被截断成 error_is_cycle == false；
 *        现在记录容量等于 active 上限，且方向口径为"下游 -> 上游"，末尾闭合回首个成员。
 *        夹具里另有两条只在下游的残余节点 Tail.0 -> Ring.0、Tail.1 -> Tail.0：
 *        它们仍有入度但不是环成员，不得被算进 error_nodes。
 */

namespace
{
    using namespace ID::Bench::Fixture;
    using namespace ID::Bench::Fixture::Cases;

    // 14 字符 + 末尾 NUL：register_plugin 以数组长度推导名字长度，行宽必须精确
    inline constexpr char kRingNames[9][15] = {
        "ID.Case.Ring.0", "ID.Case.Ring.1", "ID.Case.Ring.2", "ID.Case.Ring.3", "ID.Case.Ring.4",
        "ID.Case.Ring.5", "ID.Case.Ring.6", "ID.Case.Ring.7", "ID.Case.Ring.8"
    };
    inline constexpr char kTailNames[2][15] = { "ID.Case.Tail.0", "ID.Case.Tail.1" };

    inline constexpr size_t kRingCount = 9;
    inline constexpr size_t kTailCount = 2;

    template <bool RingClosed, size_t Index>
    consteval auto make_ring_decl() noexcept
    {
        constexpr auto identity       = register_plugin(kRingNames[Index]);
        constexpr bool has_dependency = RingClosed || Index != 0;
        if constexpr (has_dependency)
        {
            // 每个成员依赖环上的下一个：Ring.i -> Ring.(i+1)，最后回到 Ring.0
            return declare_dependencies(identity, std::tuple{ group_of(kRingNames[(Index + 1) % kRingCount]) });
        }
        else
        {
            return declare_dependencies(identity, std::tuple<>{});
        }
    }

    template <size_t Index>
    consteval auto make_tail_decl() noexcept
    {
        constexpr auto identity = register_plugin(kTailNames[Index]);
        if constexpr (Index == 0)
        {
            return declare_dependencies(identity, std::tuple{ group_of(kRingNames[0]) });
        }
        else
        {
            return declare_dependencies(identity, std::tuple{ group_of(kTailNames[0]) });
        }
    }

    template <bool RingClosed, size_t... Index>
    consteval auto make_catalog(std::index_sequence<Index...>) noexcept
    {
        return std::tuple{ make_ring_decl<RingClosed, Index>()..., make_tail_decl<0>(), make_tail_decl<1>() };
    }

    template <size_t... Index>
    consteval auto make_selected(std::index_sequence<Index...>) noexcept
    {
        return std::array<PluginIdentity, kRingCount + kTailCount>{
            register_plugin(kRingNames[Index])..., register_plugin(kTailNames[0]), register_plugin(kTailNames[1])
        };
    }

    template <bool RingClosed>
    consteval auto make_case_plan() noexcept
    {
        return sort_and_validate(make_catalog<RingClosed>(std::make_index_sequence<kRingCount>{}),
                                 make_selected(std::make_index_sequence<kRingCount>{}));
    }

    // 环成员必须全部是环上节点，且一个不少（残余节点不得混入）
    template <typename Plan>
    consteval bool cycle_is_exactly_the_ring(const Plan& plan) noexcept
    {
        if (plan.error_node_count != kRingCount)
        {
            return false;
        }
        for (size_t ring = 0; ring < kRingCount; ++ring)
        {
            const uint16_t index = find_active_index(plan, kRingNames[ring]);
            bool           found = false;
            for (uint16_t i = 0; i < plan.error_node_count; ++i)
            {
                if (plan.error_nodes[i] == index)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                return false;
            }
        }
        for (size_t tail = 0; tail < kTailCount; ++tail)
        {
            const uint16_t index = find_active_index(plan, kTailNames[tail]);
            for (uint16_t i = 0; i < plan.error_node_count; ++i)
            {
                if (plan.error_nodes[i] == index)
                {
                    return false;       // 环下游残余节点被当成了环成员
                }
            }
        }
        return true;
    }

#if defined(FIXTURE_CASE_LEGAL)
    constexpr auto kPlan = make_case_plan<false>();

    static_assert(kPlan.error == ErrorCode::None, "cycle_long: 合法对照必须组装成功");
    static_assert(kPlan.active_count == kRingCount + kTailCount, "cycle_long: 11 个 active 必须全部就位");
    // 合法对照把 Ring.0 的上游去掉：环上只剩 8 条边，加两条残余下游边共 10 条
    static_assert(kPlan.edge_count == (kRingCount - 1) + kTailCount, "cycle_long: 合法对照应有 10 条去重边");
    static_assert(kPlan.topo_order[0] == find_active_index(kPlan, kRingNames[0]),
                  "cycle_long: 合法对照的首个拓扑节点必须是无上游的 Ring.0");
    static_assert(kPlan.topo_order[kRingCount + kTailCount - 1] == find_active_index(kPlan, kTailNames[1]),
                  "cycle_long: 最后一个拓扑节点必须是最下游的 Tail.1");
#else
    constexpr auto kPlan = make_case_plan<true>();

    ID_FIXTURE_EXPECT_ERROR(kPlan, Cycle);
    static_assert(kPlan.error_is_cycle, "cycle_long: 必须是真实环路径标记");
    static_assert(kPlan.error_node_count == kRingCount, "cycle_long: 9 节点环必须完整记录 9 个成员");
    static_assert(cycle_is_exactly_the_ring(kPlan), "cycle_long: 环成员必须正好是 Ring.0..Ring.8，不含残余节点");
    static_assert(cycle_path_is_closed(kPlan), "cycle_long: 记录的成员必须按真实依赖边首尾闭合");
#endif
} // namespace
