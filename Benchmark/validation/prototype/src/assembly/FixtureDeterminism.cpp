#include <array>
#include <tuple>
#include <type_traits>

#include "FixturePluginApi.hpp"

/**
 *  编译期不变量检查（随装配库一起编译，失败即产品编译失败）
 *  1. 拥有存储：PluginIdentity / PluginDeclaration 复制后不引用原缓冲区，源被改写也不受影响；
 *  2. 平凡复制：两个类型都不允许持有需要析构或指针别名语义的成员；
 *  3. 真正确定性：打乱 catalog 顺序、选中集合顺序与组内候选顺序后，
 *     规范化的 active 表、拓扑序、逐需求 provider 与指纹必须逐项相同。
 */

namespace
{
    using namespace ID::Bench::Fixture;

    // ---------- 1. 拥有存储 ----------

    consteval bool identity_owns_name() noexcept
    {
        char buffer[32] = "ID.Det.Local";
        const PluginIdentity identity = detail::identity_from_chars(buffer, 12);
        if (!identity.is_valid)
        {
            return false;
        }
        // 把源缓冲区整块擦掉：如果身份只是 view，这里就会读到垃圾
        for (size_t i = 0; i < sizeof(buffer); ++i)
        {
            buffer[i] = 'x';
        }
        return identity.view() == "ID.Det.Local";
    }

    consteval bool declaration_owns_candidates() noexcept
    {
        char buffer[32] = "ID.Det.Source";
        const PluginIdentity source = detail::identity_from_chars(buffer, 13);
        const PluginIdentity owner  = register_plugin("ID.Det.Owner");
        const auto declaration = declare_dependencies(owner, std::tuple{ std::array<std::string_view, 1>{ source.view() } });
        for (size_t i = 0; i < sizeof(buffer); ++i)
        {
            buffer[i] = 'y';
        }
        return declaration.error == ErrorCode::None && declaration.group_count == 1 &&
               declaration.candidate_count == 1 && declaration.candidate_view(0) == "ID.Det.Source";
    }

    static_assert(std::is_trivially_copyable_v<PluginIdentity>, "PluginIdentity 必须是平凡可复制值类型");
    static_assert(std::is_trivially_copyable_v<PluginDeclaration<2, 2>>, "PluginDeclaration 必须是平凡可复制值类型");
    // 97 字节名字 + 两个前缀字（16 字节）+ ID/长度/有效位：仍应是编译期可承受的定长值类型
    static_assert(sizeof(PluginIdentity) <= 144, "身份结构不应成为编译期负担");
    static_assert(identity_owns_name(), "PluginIdentity 必须复制名字字节而不是持有指针");
    static_assert(declaration_owns_candidates(), "PluginDeclaration 必须复制候选身份而不是持有 view");

    // ---------- 2. 复制/移动后仍然有效 ----------

    consteval bool copy_keeps_content() noexcept
    {
        constexpr auto         original = register_plugin("ID.Det.Copy");
        constexpr auto         owner    = register_plugin("ID.Det.Owner2");
        constexpr auto         declaration = declare_dependencies(owner, std::tuple{ std::array<std::string_view, 1>{ "ID.Det.Copy" } });
        const PluginIdentity   identity_copy = original;
        const auto             declaration_copy = declaration;
        constexpr std::array<PluginIdentity, 2> selected{ original, owner };
        const auto plan      = sort_and_validate(std::tuple{ declaration, declare_dependencies(original, std::tuple<>{}) }, selected);
        const auto plan_copy = plan;
        return identity_copy.view() == original.view() && identity_copy.id == original.id &&
               declaration_copy.candidate_view(0) == "ID.Det.Copy" &&
               plan_copy.error == ErrorCode::None && plan_copy.active_identity[0].view() == "ID.Det.Copy";
    }

    static_assert(copy_keeps_content(), "复制后的身份/声明/计划必须仍然持有有效内容");

    // ---------- 3. 真正确定性 ----------

    template <bool Permuted>
    consteval auto make_determinism_plan() noexcept
    {
        constexpr auto alpha = register_plugin("ID.Det.Alpha");
        constexpr auto zeta  = register_plugin("ID.Det.Zeta");
        constexpr auto mid   = register_plugin("ID.Det.Mid");
        constexpr auto spare = register_plugin("ID.Det.Spare");

        // Mid 的两个需求各只有一个已选候选；group0 额外带一个"仅 catalog"候选用于打乱顺序
        const auto alpha_decl = declare_dependencies(alpha, std::tuple<>{});
        const auto zeta_decl  = declare_dependencies(zeta, std::tuple<>{});
        const auto spare_decl = declare_dependencies(spare, std::tuple<>{});

        if constexpr (Permuted)
        {
            const auto mid_decl = declare_dependencies(mid, std::tuple{
                std::array<std::string_view, 2>{ "ID.Det.Spare", "ID.Det.Alpha" },
                std::array<std::string_view, 1>{ "ID.Det.Zeta" } });
            // catalog 顺序、选中集合顺序都被打乱
            return sort_and_validate(std::tuple{ spare_decl, mid_decl, zeta_decl, alpha_decl },
                                     std::array<PluginIdentity, 3>{ mid, zeta, alpha });
        }
        else
        {
            const auto mid_decl = declare_dependencies(mid, std::tuple{
                std::array<std::string_view, 2>{ "ID.Det.Alpha", "ID.Det.Spare" },
                std::array<std::string_view, 1>{ "ID.Det.Zeta" } });
            return sort_and_validate(std::tuple{ alpha_decl, zeta_decl, spare_decl, mid_decl },
                                     std::array<PluginIdentity, 3>{ alpha, zeta, mid });
        }
    }

    template <typename Left, typename Right>
    consteval bool same_plan(const Left& left, const Right& right) noexcept
    {
        if (left.active_count != right.active_count || left.edge_count != right.edge_count)
        {
            return false;
        }
        for (uint16_t index = 0; index < left.active_count; ++index)
        {
            if (left.active_identity[index].view() != right.active_identity[index].view())
            {
                return false;
            }
            if (left.topo_order[index] != right.topo_order[index])
            {
                return false;
            }
            // 只比该 active 的实际需求组数：provider 的其余槽不再预填哨兵，不能按固定 32 扫
            if (left.active_requirement[index] != right.active_requirement[index])
            {
                return false;
            }
            for (uint16_t requirement = 0; requirement < left.active_requirement[index]; ++requirement)
            {
                const size_t   offset   = static_cast<size_t>(index) * kMaxRequirements + requirement;
                const uint16_t provider_left  = left.provider[offset];
                const uint16_t provider_right = right.provider[offset];
                if (left.active_identity[provider_left].view() != right.active_identity[provider_right].view())
                {
                    return false;
                }
            }
        }
        return true;
    }

    constexpr auto kPlain    = make_determinism_plan<false>();
    constexpr auto kPermuted = make_determinism_plan<true>();

    static_assert(kPlain.error == ErrorCode::None, "确定性用例必须合法");
    static_assert(kPermuted.error == ErrorCode::None, "打乱顺序后仍然必须合法");
    static_assert(kPlain.active_count == 3, "只应有 3 个 active（Spare 未选中）");
    static_assert(kPlain.fingerprint == kPermuted.fingerprint, "指纹必须与输入顺序无关");
    static_assert(same_plan(kPlain, kPermuted), "打乱输入顺序后规范化编号、拓扑序与绑定必须逐项相同");

    // tie-break：两个独立上游就绪时按插件名字节序先取 Alpha
    static_assert(kPlain.topo_order[0] == find_active_index(kPlain, "ID.Det.Alpha"), "就绪集合 tie-break 必须先 Alpha");
    static_assert(kPlain.topo_order[1] == find_active_index(kPlain, "ID.Det.Zeta"), "第二个必须是 Zeta");
    static_assert(kPlain.topo_order[2] == find_active_index(kPlain, "ID.Det.Mid"), "下游必须排在上游之后");
    static_assert(kPlain.provider[static_cast<size_t>(find_active_index(kPlain, "ID.Det.Mid")) * kMaxRequirements] ==
                      find_active_index(kPlain, "ID.Det.Alpha"),
                  "Mid 的 group0 必须绑定 Alpha（Spare 未选中）");

    // ---------- 4. 同一上游满足多个需求：绑定全部保留，拓扑边只计一次（计划书 6.3） ----------

    // @note 必须写成命名 consteval 函数，不能就地写成 lambda：MSVC 19.52 在 lambda 体内调用
    //       consteval 函数会报 C7595（对即时函数的调用不是常量表达式）+ 多条 C2131；
    //       命名函数的功能与本节其它用例一致，断言与语义完全相同，GCC 上同样成立。
    consteval auto make_shared_upstream_plan() noexcept
    {
        constexpr auto upstream = register_plugin("ID.Det.Shared.Up");
        constexpr auto dependent = register_plugin("ID.Det.Shared.Down");
        const auto upstream_decl = declare_dependencies(upstream, std::tuple<>{});
        // 两个不同需求，候选都只有同一个上游
        const auto dependent_decl = declare_dependencies(
            dependent, std::tuple{ std::array<std::string_view, 1>{ "ID.Det.Shared.Up" },
                                   std::array<std::string_view, 1>{ "ID.Det.Shared.Up" } });
        return sort_and_validate(std::tuple{ upstream_decl, dependent_decl },
                                 std::array<PluginIdentity, 2>{ upstream, dependent });
    }

    constexpr auto kSharedUpstream = make_shared_upstream_plan();

    static_assert(kSharedUpstream.error == ErrorCode::None, "共享上游用例必须合法");
    static_assert(kSharedUpstream.edge_count == 1, "同一上游满足两个需求时拓扑边只能计一次");
    static_assert(kSharedUpstream.provider[static_cast<size_t>(find_active_index(kSharedUpstream, "ID.Det.Shared.Down")) * kMaxRequirements + 0] ==
                          find_active_index(kSharedUpstream, "ID.Det.Shared.Up") &&
                      kSharedUpstream.provider[static_cast<size_t>(find_active_index(kSharedUpstream, "ID.Det.Shared.Down")) * kMaxRequirements + 1] ==
                          find_active_index(kSharedUpstream, "ID.Det.Shared.Up"),
                  "两个需求都必须保留各自的上游绑定记录");

    // ---------- 5. 未选后端形成的潜在环不得误判（计划书 6.3 / 16.1） ----------
    //   A 依赖 [B, C]；B 依赖 A；C 无上游。
    //   选中 {A, C}：固定图只有 C -> A，合法；
    //   选中 {A, B}：固定图 A <-> B，必须报环。

    template <bool WithBackend>
    consteval auto make_potential_cycle_plan() noexcept
    {
        constexpr auto a = register_plugin("ID.Pot.A");
        constexpr auto b = register_plugin("ID.Pot.B");
        constexpr auto c = register_plugin("ID.Pot.C");
        const auto a_decl = declare_dependencies(
            a, std::tuple{ std::array<std::string_view, 2>{ "ID.Pot.B", "ID.Pot.C" } });
        const auto b_decl = declare_dependencies(b, std::tuple{ std::array<std::string_view, 1>{ "ID.Pot.A" } });
        const auto c_decl = declare_dependencies(c, std::tuple<>{});
        if constexpr (WithBackend)
        {
            return sort_and_validate(std::tuple{ a_decl, b_decl, c_decl },
                                     std::array<PluginIdentity, 2>{ a, b });
        }
        else
        {
            return sort_and_validate(std::tuple{ a_decl, b_decl, c_decl },
                                     std::array<PluginIdentity, 2>{ a, c });
        }
    }

    constexpr auto kNoBackendCycle = make_potential_cycle_plan<false>();
    constexpr auto kBackendCycle   = make_potential_cycle_plan<true>();

    static_assert(kNoBackendCycle.error == ErrorCode::None,
                  "未选中的后端即使能形成环，也不得影响选中图的合法性");
    static_assert(kNoBackendCycle.edge_count == 1, "选中 {A, C} 时只应有一条 C -> A 边");
    static_assert(kNoBackendCycle.provider[static_cast<size_t>(find_active_index(kNoBackendCycle, "ID.Pot.A")) * kMaxRequirements] ==
                      find_active_index(kNoBackendCycle, "ID.Pot.C"),
                  "A 的需求必须绑定唯一选中的后端 C");
    static_assert(kBackendCycle.error == ErrorCode::Cycle, "选中 {A, B} 时固定图是真环，必须拒绝");
} // namespace
