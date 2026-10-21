#include "benchmark.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <thread>
#include <tuple>

#include "Common/tools/LinearArena.hpp"
#include "Common/tools/SegmentedArena.hpp"
#include "Kernel/BootstrapLog.hpp"
#include "Kernel/Memory.hpp"
#include "Kernel/Plugin.hpp"
#include "Kernel/PluginRuntime.hpp"

/**
 *  ID.Kernel 正确性测试：自举日志、类型/哈希、通用 Arena、内存、插件运行期与编译期绑定
 *  1. 覆盖 references/methodology.md 的 C1–C7：契约一致性、边界、非法输入、状态机/幂等、并发契约、内存安全、数据完整性；
 *  2. 插件部分已按 P4 迁移：运行期只走 `ID::PluginRuntime`（计划视图 + 状态机 + 槽所有权），
 *     依赖与后端选择只走编译期 `PluginManager::sort_and_validate` 的唯一绑定结论；
 *     旧的运行期单例登记、描述符指针数组与“任一候选即可”OR 语义不再作为被测契约（计划书 14.2 / 15 P4）；
 *  3. 仍属 abort 的路径（Level::Fatal 日志、report_assert）一律 Skip 并写明依据，
 *     绝不在同一个进程里故意触发 abort；运行期的可恢复错误必须真实断言，不再以“已知 abort”跳过；
 *  4. 每个用例自包含（自己分配自己释放、自己开关模式），失败只记 Check，不影响后续用例。
 */

namespace
{
    // ========================= 日志捕获 =========================
    // 默认 sink 会写 stderr；测试期间整体接管，避免内核日志污染 console 与 report.txt 的一致性

    struct CapturedLog
    {
        ID::Log::Level  level               = ID::Log::Level::Trace;
        char            category[32]        = {};
        char            message[160]        = {};
    };

    CapturedLog g_last_log{};
    ID::size_t  g_log_count = 0;

    void copy_truncated(char* destination, ID::size_t capacity, std::string_view source) noexcept
    {
        ID::size_t length = source.size();
        if (length >= capacity)
        {
            length = capacity - 1;
        }
        if (length != 0)
        {
            std::memcpy(destination, source.data(), length);
        }
        destination[length] = '\0';
    }

    void capturing_sink(ID::Log::Level level, std::string_view category, std::string_view message) noexcept
    {
        g_last_log.level = level;
        copy_truncated(g_last_log.category, sizeof(g_last_log.category), category);
        copy_truncated(g_last_log.message, sizeof(g_last_log.message), message);
        ++g_log_count;
    }

    // ========================= 生命周期记录 =========================
    // 每个插件用模板参数区分身份：回调无参数，只能靠不同的函数实例承载"我是谁"

    char        g_lifecycle[64]    = {};
    ID::size_t  g_lifecycle_length = 0;

    void reset_lifecycle() noexcept
    {
        std::memset(g_lifecycle, 0, sizeof(g_lifecycle));
        g_lifecycle_length = 0;
    }

    void record_lifecycle(char phase, char who) noexcept
    {
        if (g_lifecycle_length + 2 < sizeof(g_lifecycle))
        {
            g_lifecycle[g_lifecycle_length++] = phase;
            g_lifecycle[g_lifecycle_length++] = who;
            g_lifecycle[g_lifecycle_length]   = '\0';
        }
    }

    void check_lifecycle(const char* expected, std::string_view name) noexcept
    {
        char detail[160];
        std::snprintf(detail, sizeof(detail), "期望 %s 实际 %s", expected, g_lifecycle);
        ID::Bench::Check(std::strcmp(g_lifecycle, expected) == 0, name, detail);
    }

    // ========================= 运行期角色模式 =========================
    // 回调签名是固定的无参 noexcept 函数，用例需要的差异只能靠这几个开关表达；
    // 开关只在测试进程内读写，进入每个用例前由 reset_case_state() 复位。

    enum class SlotAction : ID::uint8_t
    {
        None = 0,
        PublishBoth,            // 发布自己的两个槽（合法路径）
        PublishTwice,           // 同一槽发布两次：第二次必须被拒
        PublishForeign,         // 写别的插件拥有的槽
        PublishOutOfRange,      // 槽 64（MaxSlots 哨兵本身）
        PublishNullThenBoth     // 先 publish(nullptr) 探测，再正常发布
    };

    constexpr ID::uint32_t kMathSlot    = static_cast<ID::uint32_t>(ID::ServiceSlot::Math);      // 槽 2
    constexpr ID::uint32_t kPhysicsSlot = static_cast<ID::uint32_t>(ID::ServiceSlot::Physics);   // 槽 5

    constinit SlotAction             g_slot_action    = SlotAction::None;                    // 注册期要执行的槽位动作
    constinit char                   g_slot_actor     = 'A';                                 // 执行槽位动作的角色
    constinit char                   g_init_fail_role = '\0';                                // on_init 返回 false 的角色
    constinit bool                   g_reentry_probe  = false;                               // init 阶段是否探测 shutdown 重入
    constinit ID::PluginRuntimeError g_action_code    = ID::PluginRuntimeError::None;        // 动作第一条返回码
    constinit ID::PluginRuntimeError g_action_code_2  = ID::PluginRuntimeError::None;        // 动作第二条返回码
    constinit ID::PluginRuntimeError g_reentry_code   = ID::PluginRuntimeError::None;        // 重入探测返回码

    // ---- 服务槽测试用函数表 ----
    struct FakeApiTable
    {
        ID::uint32_t magic = 0;
    };

    constexpr FakeApiTable kApiTableOne{ 0x11111111u };
    constexpr FakeApiTable kApiTableTwo{ 0x22222222u };

    /**
     *  @brief 把当前用例的槽位动作施加到运行期（只在 on_register 内被调用）
     *  @note 每次 publish 的返回码都留在 g_action_code / g_action_code_2，供用例区分
     *        “被拒绝”和“根本没调用”；这里从不断言，判定一律交给 Check
     */
    void apply_slot_action() noexcept
    {
        ID::PluginRuntime& runtime = ID::PluginRuntime::instance();

        switch (g_slot_action)
        {
            case SlotAction::None:
                break;
            case SlotAction::PublishBoth:
                g_action_code   = runtime.publish_service(kMathSlot, &kApiTableOne).code;
                g_action_code_2 = runtime.publish_service(kPhysicsSlot, &kApiTableTwo).code;
                break;
            case SlotAction::PublishNullThenBoth:
                // nullptr 不再表示“卸下”：发布空表必须被拒绝，注销只能走 revoke_service
                g_action_code   = runtime.publish_service(kMathSlot, nullptr).code;
                g_action_code_2 = runtime.publish_service(kMathSlot, &kApiTableOne).code;
                static_cast<void>(runtime.publish_service(kPhysicsSlot, &kApiTableTwo));
                break;
            case SlotAction::PublishTwice:
                g_action_code   = runtime.publish_service(kMathSlot, &kApiTableOne).code;
                g_action_code_2 = runtime.publish_service(kMathSlot, &kApiTableTwo).code;
                static_cast<void>(runtime.publish_service(kPhysicsSlot, &kApiTableTwo));
                break;
            case SlotAction::PublishForeign:
                g_action_code   = runtime.publish_service(kMathSlot, &kApiTableTwo).code;
                g_action_code_2 = runtime.publish_service(kPhysicsSlot, &kApiTableTwo).code;
                break;
            case SlotAction::PublishOutOfRange:
                g_action_code   = runtime.publish_service(64, &kApiTableOne).code;
                static_cast<void>(runtime.publish_service(kMathSlot, &kApiTableOne));
                static_cast<void>(runtime.publish_service(kPhysicsSlot, &kApiTableTwo));
                break;
        }
    }

    template <char Who>
    void plugin_on_register() noexcept
    {
        record_lifecycle('R', Who);
        if (Who == 'A' && g_slot_action != SlotAction::None && g_slot_actor != 'A')
        {
            // 被判定的越权动作属于别的角色：合法 owner A 仍要先发布自己的槽，
            // 否则流程会停在 RegisterSlotMissing，而不是停在真正想验证的越权发布
            static_cast<void>(ID::PluginRuntime::instance().publish_service(kMathSlot, &kApiTableOne));
            static_cast<void>(ID::PluginRuntime::instance().publish_service(kPhysicsSlot, &kApiTableTwo));
        }
        if (Who == g_slot_actor)
        {
            apply_slot_action();
        }
    }

    template <char Who>
    bool plugin_on_init() noexcept
    {
        record_lifecycle('I', Who);
        if (g_reentry_probe && Who == 'B')
        {
            // 回调期间重入：必须返回 Busy，且不改变当前流程
            g_reentry_code = ID::PluginRuntime::instance().shutdown().code;
        }
        return Who != g_init_fail_role;
    }

    template <char Who>
    void plugin_on_shutdown() noexcept
    {
        record_lifecycle('S', Who);
    }

    template <char Who>
    void plugin_on_unload() noexcept
    {
        record_lifecycle('U', Who);
        if (Who == 'A' && g_slot_action != SlotAction::None)
        {
            // 只注销自己的槽；角色 B 从不 revoke，避免把“注销别人的槽”混进正常路径
            static_cast<void>(ID::PluginRuntime::instance().revoke_service(kMathSlot));
            static_cast<void>(ID::PluginRuntime::instance().revoke_service(kPhysicsSlot));
        }
    }

    // ---- 运行期记录：四个角色 A/B/C/D；记录、拓扑序与槽归属全部是静态存储期的常量数据 ----

    inline constexpr ID::PluginRuntimeCallbacks kCallbacksA{
        &plugin_on_register<'A'>, &plugin_on_init<'A'>, &plugin_on_shutdown<'A'>, &plugin_on_unload<'A'> };
    inline constexpr ID::PluginRuntimeCallbacks kCallbacksB{
        &plugin_on_register<'B'>, &plugin_on_init<'B'>, &plugin_on_shutdown<'B'>, &plugin_on_unload<'B'> };
    inline constexpr ID::PluginRuntimeCallbacks kCallbacksC{
        &plugin_on_register<'C'>, &plugin_on_init<'C'>, &plugin_on_shutdown<'C'>, &plugin_on_unload<'C'> };
    inline constexpr ID::PluginRuntimeCallbacks kCallbacksD{
        &plugin_on_register<'D'>, &plugin_on_init<'D'>, &plugin_on_shutdown<'D'>, &plugin_on_unload<'D'> };

    inline constexpr ID::PluginRuntimeRecord kRecordA{
        .name      = "Bench.Kernel.A",
        .plugin_id = ID::hash_fnv_1a64("Bench.Kernel.A"),
        .callbacks = kCallbacksA
    };
    inline constexpr ID::PluginRuntimeRecord kRecordB{
        .name      = "Bench.Kernel.B",
        .plugin_id = ID::hash_fnv_1a64("Bench.Kernel.B"),
        .callbacks = kCallbacksB
    };
    inline constexpr ID::PluginRuntimeRecord kRecordC{
        .name      = "Bench.Kernel.C",
        .plugin_id = ID::hash_fnv_1a64("Bench.Kernel.C"),
        .callbacks = kCallbacksC
    };
    inline constexpr ID::PluginRuntimeRecord kRecordD{
        .name      = "Bench.Kernel.D",
        .plugin_id = ID::hash_fnv_1a64("Bench.Kernel.D"),
        .callbacks = kCallbacksD
    };

    /**
     *  @brief 64 项槽归属表
     *  @note 默认全部 kNoIndex（空槽）：这正是“无服务插件”的合法空表，运行期据此判断槽是否被计划占用
     */
    struct SlotOwnerTable
    {
        ID::uint16_t cells[ID::PluginRuntime::kMaxSlots];

        constexpr SlotOwnerTable() noexcept
            : cells{}
        {
            for (ID::size_t slot = 0; slot < ID::PluginRuntime::kMaxSlots; ++slot)
            {
                cells[slot] = ID::kNoIndex;
            }
        }

        constexpr void set(ID::size_t slot, ID::uint16_t owner) noexcept
        {
            cells[slot] = owner;
        }
    };

    inline constexpr SlotOwnerTable kOwnersEmpty{};

    // ---- 计划 1：链式 A -> B -> C；records 故意倒序（C,B,A），topo_order 显式给出 A,B,C ----
    // 运行期不做拓扑求解，它必须逐字执行计划给定的 topo_order（计划书 10.1 / 10.4）
    inline constexpr ID::PluginRuntimeRecord kChainRecords[3] = { kRecordC, kRecordB, kRecordA };
    inline constexpr ID::uint16_t            kChainTopo[3]    = { 2, 1, 0 };

    inline constexpr ID::PluginRuntimePlanView kPlanChain{
        .plugin_count    = 3,
        .provision_count = 0,
        .records         = kChainRecords,
        .topo_order      = kChainTopo,
        .provisions      = nullptr,
        .slot_owner      = kOwnersEmpty.cells
    };

    inline constexpr char kSequenceChainStart[] = "RARBRCIAIBIC";
    inline constexpr char kSequenceChainFull[]  = "RARBRCIAIBICSCSBSAUCUBUA";

    // ---- 计划 2：菱形 A -> {B,C} -> D；同样用倒序 records + 正序 topo 证明只认计划 ----
    inline constexpr ID::PluginRuntimeRecord kDiamondRecords[4] = { kRecordD, kRecordC, kRecordB, kRecordA };
    inline constexpr ID::uint16_t            kDiamondTopo[4]    = { 3, 2, 1, 0 };

    inline constexpr ID::PluginRuntimePlanView kPlanDiamond{
        .plugin_count    = 4,
        .provision_count = 0,
        .records         = kDiamondRecords,
        .topo_order      = kDiamondTopo,
        .provisions      = nullptr,
        .slot_owner      = kOwnersEmpty.cells
    };

    inline constexpr char kSequenceDiamondFull[] = "RARBRCRDIAIBICIDSDSCSBSAUDUCUBUA";

    // ---- 计划 3：零插件（显式空数组：计数为 0，指针指向真实静态数组） ----
    inline constexpr ID::PluginRuntimePlanView kPlanZero{
        .plugin_count    = 0,
        .provision_count = 0,
        .records         = kChainRecords,
        .topo_order      = kChainTopo,
        .provisions      = nullptr,
        .slot_owner      = kOwnersEmpty.cells
    };

    // ---- 计划 4：容量上限 128（无回调，只验证计数接受与状态迁移） ----
    inline constexpr ID::size_t kCapacityPluginCount = ID::PluginRuntime::kMaxPlugins;

    struct CapacityRecords
    {
        ID::PluginRuntimeRecord records[kCapacityPluginCount]{};
        ID::uint16_t            topo[kCapacityPluginCount]{};

        constexpr CapacityRecords() noexcept
        {
            for (ID::size_t index = 0; index < kCapacityPluginCount; ++index)
            {
                records[index].name      = "Bench.Kernel.Generated";
                records[index].plugin_id = ID::hash_fnv_1a64("Bench.Kernel.Generated") + index;
                records[index].callbacks = {};
                topo[index]              = static_cast<ID::uint16_t>(index);
            }
        }
    };

    inline constexpr CapacityRecords kCapacityRecords{};

    inline constexpr ID::PluginRuntimePlanView kPlanCapacity{
        .plugin_count    = static_cast<ID::uint16_t>(kCapacityPluginCount),
        .provision_count = 0,
        .records         = kCapacityRecords.records,
        .topo_order      = kCapacityRecords.topo,
        .provisions      = nullptr,
        .slot_owner      = kOwnersEmpty.cells
    };

    // ---- 计划 5：单插件 A 拥有槽 2（Math）与槽 5（Physics） ----
    inline constexpr ID::PluginRuntimeRecord kSlotRecords[1] = { kRecordA };
    inline constexpr ID::uint16_t            kSlotTopo[1]    = { 0 };
    inline constexpr ID::uint16_t            kPairTopo[2]    = { 0, 1 };

    inline constexpr ID::PluginRuntimeProvision kSlotProvisions[2] = {
        { .capability = "bench.kernel.math",    .api_revision = 1, .plugin_index = 0,
          .slot = static_cast<int16_t>(kMathSlot) },
        { .capability = "bench.kernel.physics", .api_revision = 1, .plugin_index = 0,
          .slot = static_cast<int16_t>(kPhysicsSlot) }
    };

    constexpr SlotOwnerTable make_slot_owner_table() noexcept
    {
        SlotOwnerTable table{};
        table.set(kMathSlot, 0);
        table.set(kPhysicsSlot, 0);
        return table;
    }

    inline constexpr SlotOwnerTable kOwnersSlotA = make_slot_owner_table();

    inline constexpr ID::PluginRuntimePlanView kPlanSlot{
        .plugin_count    = 1,
        .provision_count = 2,
        .records         = kSlotRecords,
        .topo_order      = kSlotTopo,
        .provisions      = kSlotProvisions,
        .slot_owner      = kOwnersSlotA.cells
    };

    void reset_case_state() noexcept
    {
        reset_lifecycle();
        g_slot_action    = SlotAction::None;
        g_slot_actor     = 'A';
        g_init_fail_role = '\0';
        g_reentry_probe  = false;
        g_action_code    = ID::PluginRuntimeError::None;
        g_action_code_2  = ID::PluginRuntimeError::None;
        g_reentry_code   = ID::PluginRuntimeError::None;
    }

    bool all_slots_empty() noexcept
    {
        const ID::PluginRuntime& runtime = ID::PluginRuntime::instance();
        for (ID::size_t slot = 0; slot < ID::PluginRuntime::kMaxSlots; ++slot)
        {
            if (runtime.get_service<void>(static_cast<ID::ServiceSlot>(slot)) != nullptr)
            {
                return false;
            }
        }
        return true;
    }

    // ========================= 编译期唯一绑定（替代旧运行期 OR 语义） =========================
    // 计划书 6.2 / 6.4：某需求在“选中集合”里恰好命中 1 个候选才算绑定成功；0 个是 MissingProvider，
    // 2 个及以上是 AmbiguousProvider —— 先判名称唯一，再看接口修订，绝不因为某个候选版本不足就改选另一个。
    // 运行期不再搜索候选、也不读版本号，所以这几条只能按编译期结论断言。

    namespace CP = ID::PluginCompileTime;

    inline constexpr char kCapabilityMath[]     = "Bench.Bind.Cap.Math";
    inline constexpr char kCapabilityConsumer[] = "Bench.Bind.Cap.Consumer";
    inline constexpr char kCapabilityLeft[]     = "Bench.Bind.Cap.Left";
    inline constexpr char kCapabilityRight[]    = "Bench.Bind.Cap.Right";

    inline constexpr auto kConsumerIdentity = ID::PluginManager::register_plugin("Bench.Bind.Consumer");
    inline constexpr auto kSimdIdentity     = ID::PluginManager::register_plugin("Bench.Bind.Math.Simd");
    inline constexpr auto kFixedIdentity    = ID::PluginManager::register_plugin("Bench.Bind.Math.Fixed");
    inline constexpr auto kLeftIdentity     = ID::PluginManager::register_plugin("Bench.Bind.Left");
    inline constexpr auto kRightIdentity    = ID::PluginManager::register_plugin("Bench.Bind.Right");

    /**
     *  @brief 构造一个 catalog 条目：依赖声明 + 完整元数据（契约条数与逐候选修订按声明规模展开）
     *  @param groups 依赖的二维名字清单（外层 AND、内层 OR）
     *  @param candidate_names 与 groups 摊平顺序一致的候选名（静态存储）
     *  @param capabilities 每个需求组的能力名，长度必须等于组数
     *  @note 修订记录必须引用调用方的静态名字，不能引用 declaration.candidate_view()：
     *        后者指向条目自己的字节池，条目一旦复制即成悬空 view
     */
    template <size_t ProvidesCapacity, typename Identity, typename... Groups, size_t CandidateCount>
    consteval auto make_catalog_entry(Identity identity,
                                      const std::tuple<Groups...>& groups,
                                      const std::array<std::string_view, CandidateCount>& candidate_names,
                                      const std::array<std::string_view, sizeof...(Groups)>& capabilities,
                                      std::string_view target,
                                      std::string_view cpp_namespace,
                                      const std::array<CP::Provision, ProvidesCapacity>& provides,
                                      ID::uint32_t min_revision = 1,
                                      ID::uint32_t max_revision = 1) noexcept
        -> CP::PluginCatalogEntry<sizeof...(Groups), CandidateCount, ProvidesCapacity>
    {
        CP::PluginCatalogEntry<sizeof...(Groups), CandidateCount, ProvidesCapacity> entry{};
        entry.declaration = ID::PluginManager::declare_dependencies(identity, groups);

        CP::PluginMetadata<sizeof...(Groups), CandidateCount, ProvidesCapacity>& metadata = entry.metadata;
        metadata.target         = target;
        metadata.cpp_namespace  = cpp_namespace;
        metadata.kind           = CP::PluginKind::Module;
        metadata.lifecycle      = CP::PluginLifecycle::Standard;
        metadata.version        = 1;
        metadata.contract_count = static_cast<ID::uint16_t>(sizeof...(Groups));
        metadata.provides_count = static_cast<ID::uint16_t>(ProvidesCapacity);
        for (ID::size_t provide = 0; provide < ProvidesCapacity; ++provide)
        {
            metadata.provides[provide] = provides[provide];
        }

        ID::size_t revision_cursor = 0;
        for (ID::size_t group = 0; group < sizeof...(Groups); ++group)
        {
            const ID::uint16_t begin = entry.declaration.group_begin[group];
            const ID::uint16_t end   = entry.declaration.group_begin[group + 1];
            const ID::uint16_t count = static_cast<ID::uint16_t>(end - begin);
            metadata.contracts[group] = CP::RequirementContract{
                capabilities[group],
                static_cast<ID::uint16_t>(revision_cursor),
                count
            };
            for (ID::uint16_t candidate = 0; candidate < count; ++candidate)
            {
                metadata.revisions[revision_cursor + candidate] = CP::CandidateRevision{
                    candidate_names[revision_cursor + candidate], min_revision, max_revision
                };
            }
            revision_cursor += count;
        }
        return entry;
    }

    /**
     *  @brief 构造一个不依赖上游的提供者条目（纯头文件能力，无服务槽位）
     */
    consteval auto make_leaf_entry(CP::PluginIdentity identity, std::string_view capability,
                                   ID::uint32_t api_revision,
                                   std::string_view target, std::string_view cpp_namespace) noexcept
        -> CP::PluginCatalogEntry<0, 0, 1>
    {
        return make_catalog_entry<1>(identity, std::tuple<>{},
                                     std::array<std::string_view, 0>{},
                                     std::array<std::string_view, 0>{},
                                     target, cpp_namespace,
                                     std::array<CP::Provision, 1>{
                                         CP::Provision{ capability, api_revision, CP::kNoSlot } });
    }

    /**
     *  @brief 两个数学后端 + 一个消费者：后端逐候选的修订要求由参数给出
     *  @param fixed_min_revision / fixed_max_revision 定点后端被要求满足的接口修订区间
     *  @note 只改这两项就得到“两候选都达标”与“一旧一新”两种输入，其余条件完全相同
     */
    consteval auto make_backend_catalog(ID::uint32_t fixed_min_revision, ID::uint32_t fixed_max_revision) noexcept
    {
        const std::array<std::string_view, 2> candidates{ "Bench.Bind.Math.Simd", "Bench.Bind.Math.Fixed" };

        auto consumer = make_catalog_entry<1>(
            kConsumerIdentity,
            std::tuple{ candidates },
            candidates,
            std::array<std::string_view, 1>{ kCapabilityMath },
            "BenchBindConsumer", "Bench::Bind::Consumer",
            std::array<CP::Provision, 1>{ CP::Provision{ kCapabilityConsumer, 1, CP::kNoSlot } });
        consumer.metadata.revisions[0] = CP::CandidateRevision{ "Bench.Bind.Math.Simd", 2, 2 };
        consumer.metadata.revisions[1] = CP::CandidateRevision{ "Bench.Bind.Math.Fixed", fixed_min_revision, fixed_max_revision };

        // Simd 是修订 2 的新后端，Fixed 是修订 1 的旧后端
        return std::tuple{
            consumer,
            make_leaf_entry(kSimdIdentity, kCapabilityMath, 2, "BenchBindMathSimd", "Bench::Bind::MathSimd"),
            make_leaf_entry(kFixedIdentity, kCapabilityMath, 1, "BenchBindMathFixed", "Bench::Bind::MathFixed")
        };
    }

    /**
     *  @brief 两个互相依赖的插件：Left 需要 Right 的能力，Right 需要 Left 的能力
     */
    consteval auto make_cycle_catalog() noexcept
    {
        const std::array<std::string_view, 1> left_candidates{ "Bench.Bind.Right" };
        const std::array<std::string_view, 1> right_candidates{ "Bench.Bind.Left" };

        return std::tuple{
            make_catalog_entry<1>(kLeftIdentity, std::tuple{ left_candidates }, left_candidates,
                                  std::array<std::string_view, 1>{ kCapabilityRight },
                                  "BenchBindLeft", "Bench::Bind::Left",
                                  std::array<CP::Provision, 1>{ CP::Provision{ kCapabilityLeft, 1, CP::kNoSlot } }),
            make_catalog_entry<1>(kRightIdentity, std::tuple{ right_candidates }, right_candidates,
                                  std::array<std::string_view, 1>{ kCapabilityLeft },
                                  "BenchBindRight", "Bench::Bind::Right",
                                  std::array<CP::Provision, 1>{ CP::Provision{ kCapabilityRight, 1, CP::kNoSlot } })
        };
    }

    using BackendCatalog = decltype(make_backend_catalog(1, 1));
    using CycleCatalog   = decltype(make_cycle_catalog());

    inline constexpr BackendCatalog kBackendCatalogBoth      = make_backend_catalog(1, 1);
    inline constexpr BackendCatalog kBackendCatalogOldAndNew = make_backend_catalog(2, 2);
    inline constexpr CycleCatalog   kCycleCatalog            = make_cycle_catalog();

    template <typename Shape>
    consteval CP::AssemblyInput<Shape> make_assembly_input(
        const typename Shape::CatalogTypeAlias& catalog,
        const std::array<CP::PluginIdentity, Shape::kActiveCapacity>& selected) noexcept
    {
        CP::AssemblyInput<Shape> input{};
        input.catalog  = catalog;
        input.selected = selected;
        return input;
    }

    // 证据容量只给最小合法值：下面四条都在 Selection / Graph 阶段失败，早于证据校验
    using AmbiguousShape = CP::AssemblyShape<BackendCatalog, 3, 1, 1, 1>;
    using MissingShape   = CP::AssemblyShape<BackendCatalog, 1, 1, 1, 1>;
    using CycleShape     = CP::AssemblyShape<CycleCatalog, 2, 1, 1, 1>;

    inline constexpr auto kSelectedBoth = std::array<CP::PluginIdentity, 3>{
        kConsumerIdentity, kSimdIdentity, kFixedIdentity };

    inline constexpr auto kPlanAmbiguous = ID::PluginManager::sort_and_validate(
        make_assembly_input<AmbiguousShape>(kBackendCatalogBoth, kSelectedBoth));
    inline constexpr auto kPlanOldAndNew = ID::PluginManager::sort_and_validate(
        make_assembly_input<AmbiguousShape>(kBackendCatalogOldAndNew, kSelectedBoth));
    inline constexpr auto kPlanMissing = ID::PluginManager::sort_and_validate(
        make_assembly_input<MissingShape>(kBackendCatalogBoth,
            std::array<CP::PluginIdentity, 1>{ kConsumerIdentity }));
    inline constexpr auto kPlanCycle = ID::PluginManager::sort_and_validate(
        make_assembly_input<CycleShape>(kCycleCatalog,
            std::array<CP::PluginIdentity, 2>{ kLeftIdentity, kRightIdentity }));

    // ---- LinearArena 耗尽回调 ----
    ID::size_t g_arena_fallback_calls     = 0;
    ID::size_t g_arena_fallback_size      = 0;
    ID::size_t g_arena_fallback_alignment = 0;

    void arena_fallback(ID::size_t size, ID::size_t alignment) noexcept
    {
        ++g_arena_fallback_calls;
        g_arena_fallback_size      = size;
        g_arena_fallback_alignment = alignment;
    }
} // namespace

namespace ID::Bench
{
    void IDKernelSecurityTest() noexcept
    {
        // =================================================================
        // 类型与哈希：全部 consteval，运行时零成本，只验证语义
        // =================================================================
        Check(ID::type_id<ID::int32_t>() == ID::type_id<ID::int32_t>(),
              "type_id.deterministic", "同一类型的 type_id 必须稳定");
        Check(ID::type_id<ID::int32_t>() != ID::type_id<ID::uint32_t>()
           && ID::type_id<float>()        != ID::type_id<double>()
           && ID::type_id<ID::int64_t>()  != ID::type_id<ID::uint64_t>(),
              "type_id.distinct", "不同整型/浮点类型的 type_id 必须互不相同");
        Check(ID::hash_fnv_1a64("") == 14695981039346656037ull,
              "hash.fnv.offset_basis", "空串必须等于 FNV-1a 64 位偏移基数");
        Check(ID::hash_fnv_1a64("ID.Kernel") == ID::hash_fnv_1a64("ID.Kernel")
           && ID::hash_fnv_1a64("ID.Kernel") != ID::hash_fnv_1a64("ID.Kernel "),
              "hash.fnv.distinct", "相同输入同值、不同输入异值");

        // =================================================================
        // 自举日志：默认 sink 检查 -> 接管 -> 后续内核日志不再写 stderr
        // =================================================================
        Check(ID::Log::BootstrapLog::instance().get_sink() == &ID::Log::default_bootstrap_sink,
              "log.default_sink", "未接管前 sink 必须是 default_bootstrap_sink");
        ID::Log::BootstrapLog::instance().set_sink(&capturing_sink);
        Check(ID::Log::BootstrapLog::instance().get_sink() == &capturing_sink,
              "log.sink_swap", "set_sink 之后 get_sink 必须返回新 sink");
        Skip("log.assert_dispatch", "report_assert 最终走 Level::Fatal -> abort，需子进程隔离");
        Skip("log.set_sink_nullptr", "契约是断言失败后 abort，需子进程隔离");

        // =================================================================
        // 通用 Arena：LinearArena（固定缓冲区）/ SegmentedArena（按段来源增长）
        // =================================================================
        {
            alignas(ID::kCacheLineSize) ID::uint8_t buffer[1024]{};
            ID::LinearArena arena(buffer, sizeof(buffer), &arena_fallback);

            Check(arena.is_initialized() && arena.capacity() == sizeof(buffer),
                  "arena.linear.init", "绑定缓冲区后 is_initialized 为真且 capacity 等于缓冲区大小");

            void* first_block  = arena.allocate(64, 16);
            void* second_block = arena.allocate(64, 16);
            const bool bump_ok = first_block != nullptr
                              && second_block != nullptr
                              && (reinterpret_cast<ID::uintptr_t>(first_block) & 15u) == 0
                              && (reinterpret_cast<ID::uintptr_t>(second_block) & 15u) == 0
                              && reinterpret_cast<ID::uintptr_t>(second_block) > reinterpret_cast<ID::uintptr_t>(first_block)
                              && reinterpret_cast<ID::uintptr_t>(second_block) - reinterpret_cast<ID::uintptr_t>(first_block) >= 64;
            Check(bump_ok, "arena.linear.bump_align", "连续分配必须单调上移、16B 对齐且互不重叠");
            Check(arena.used_bytes() + arena.remaining_bytes() == arena.capacity(),
                  "arena.linear.used_remaining", "used + remaining 必须恒等于 capacity");

            g_arena_fallback_calls = 0;
            void* too_big = arena.allocate(4096, 16);
            Check(too_big == nullptr
               && g_arena_fallback_calls == 1
               && g_arena_fallback_size == 4096
               && g_arena_fallback_alignment == 16,
                  "arena.linear.exhaust_fallback", "缓冲区不足必须返回 nullptr 并回调 (size, alignment)");

            arena.reset();
            Check(arena.allocate(64, 16) == static_cast<void*>(buffer) && arena.used_bytes() == 64,
                  "arena.linear.reset_rewinds", "reset 后第一次分配必须回到缓冲区起点");
        }

        {
            ID::SegmentedArena segmented;
            const bool init_ok = segmented.init(4096, &ID::Memory::ID_AlignedAlloc, &ID::Memory::ID_AlignedFree);
            Check(init_ok, "arena.segmented.init", "配置 4KB 段与内核分配器来源应成功");
            Check(!segmented.init(4096, &ID::Memory::ID_AlignedAlloc, &ID::Memory::ID_AlignedFree),
                  "arena.segmented.init_twice", "重复 init 必须返回 false（需先 shutdown）");

            bool grow_ok = true;
            for (ID::size_t i = 0; i < 64 && grow_ok; ++i)
            {
                grow_ok = segmented.allocate(256, 16) != nullptr;
            }
            Check(grow_ok && segmented.block_count() >= 2,
                  "arena.segmented.grow", "段满后必须自动向来源申请新段");
            Check(segmented.allocate(8000, 16) == nullptr,
                  "arena.segmented.oversize", "单段放不下的请求必须返回 nullptr");

            segmented.reset();
            Check(segmented.allocate(256, 16) != nullptr && segmented.block_count() >= 2,
                  "arena.segmented.reset_reuse", "reset 必须保留已申请的段并从头复用");
            segmented.shutdown();
            Check(!segmented.is_initialized() && segmented.block_count() == 0,
                  "arena.segmented.shutdown", "shutdown 必须归还全部段并复位");
        }

        // =================================================================
        // 内存：C1 契约一致性
        // =================================================================
        ID::Memory::ID_MemoryInit();

        void* contract_block = ID::Memory::ID_AlignedAlloc(64, 32);
        Check(contract_block != nullptr, "memory.alloc.non_null", "64B/32 应返回非空");
        Check(contract_block != nullptr && (reinterpret_cast<ID::uintptr_t>(contract_block) & 31u) == 0,
              "memory.alloc.alignment_32", "返回值必须满足请求的 32B 对齐");
        ID::Memory::ID_AlignedFree(contract_block);

        // =================================================================
        // 内存：C2 边界 + C6 内存安全（所有档位边界尺寸，且互不重叠）
        // =================================================================
        constexpr ID::size_t kBoundarySizes[] =
        {
            1, 16, 17, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 2048, 4096, 8192
        };
        constexpr ID::size_t kBoundaryCount = sizeof(kBoundarySizes) / sizeof(kBoundarySizes[0]);

        void* boundary_blocks[kBoundaryCount] = {};
        bool  boundary_alloc_ok = true;
        for (ID::size_t i = 0; i < kBoundaryCount; ++i)
        {
            boundary_blocks[i] = ID::Memory::ID_AlignedAlloc(kBoundarySizes[i], 16);
            boundary_alloc_ok = boundary_alloc_ok
                             && boundary_blocks[i] != nullptr
                             && (reinterpret_cast<ID::uintptr_t>(boundary_blocks[i]) & 15u) == 0;
        }
        Check(boundary_alloc_ok, "memory.alloc.size_class_edges",
              "1B..8KB 每个档位边界都必须成功并 16B 对齐");

        // 每块写满自己的独立模式：只要两块有重叠，后写的必然破坏前一块
        for (ID::size_t i = 0; i < kBoundaryCount; ++i)
        {
            if (boundary_blocks[i] != nullptr)
            {
                std::memset(boundary_blocks[i], static_cast<int>(0x10 + i), kBoundarySizes[i]);
            }
        }
        bool boundary_intact = true;
        for (ID::size_t i = 0; i < kBoundaryCount; ++i)
        {
            if (boundary_blocks[i] == nullptr)
            {
                boundary_intact = false;
                continue;
            }
            const auto* bytes = static_cast<const ID::uint8_t*>(boundary_blocks[i]);
            for (ID::size_t b = 0; b < kBoundarySizes[i]; ++b)
            {
                if (bytes[b] != static_cast<ID::uint8_t>(0x10 + i))
                {
                    boundary_intact = false;
                    break;
                }
            }
        }
        Check(boundary_intact, "memory.alloc.no_overlap", "同时存活的小对象不得互相覆盖");
        for (ID::size_t i = 0; i < kBoundaryCount; ++i)
        {
            ID::Memory::ID_AlignedFree(boundary_blocks[i]);
        }

        // 8193 = 小对象上限 + 1，必须改走大对象页
        {
            constexpr ID::size_t kLargeSize = ID::Memory::kMaxSmallBlockSize + 1;
            auto* large_block = static_cast<ID::uint8_t*>(ID::Memory::ID_AlignedAlloc(kLargeSize, 16));
            bool large_ok = large_block != nullptr;
            if (large_ok)
            {
                large_block[0]               = 0x5A;
                large_block[kLargeSize - 1]  = 0xA5;
                large_ok = large_block[0] == 0x5A && large_block[kLargeSize - 1] == 0xA5;
                ID::Memory::ID_AlignedFree(large_block);
            }
            Check(large_ok, "memory.alloc.above_small_max", "8193B 必须走大对象路径且首尾字节可读写");
        }

        // 超过小对象上限的对齐（16KB/64KB/128KB）必须由大对象页满足
        {
            constexpr ID::size_t kLargeAlignments[] = { 16384, 65536, 131072 };
            bool large_alignment_ok = true;
            for (ID::size_t i = 0; i < sizeof(kLargeAlignments) / sizeof(kLargeAlignments[0]); ++i)
            {
                const ID::size_t alignment = kLargeAlignments[i];
                void* aligned_block = ID::Memory::ID_AlignedAlloc(64, alignment);
                const bool ok = aligned_block != nullptr
                             && (reinterpret_cast<ID::uintptr_t>(aligned_block) & (alignment - 1)) == 0;
                if (ok)
                {
                    std::memset(aligned_block, 0x77, 64);
                }
                large_alignment_ok = large_alignment_ok && ok;
                ID::Memory::ID_AlignedFree(aligned_block);
            }
            Check(large_alignment_ok, "memory.alloc.large_alignment", "16KB/64KB/128KB 对齐请求必须被满足");
        }

        // =================================================================
        // 内存：C3 非法输入与归一化规则
        // =================================================================
        void* zero_size_block = ID::Memory::ID_AlignedAlloc(0, 16);
        Check(zero_size_block != nullptr, "memory.alloc.size_zero", "size 0 按 1 字节处理，必须返回非空");
        ID::Memory::ID_AlignedFree(zero_size_block);

        void* zero_alignment_block = ID::Memory::ID_AlignedAlloc(32, 0);
        Check(zero_alignment_block != nullptr
           && (reinterpret_cast<ID::uintptr_t>(zero_alignment_block) & 15u) == 0,
              "memory.alloc.alignment_zero", "alignment 0 必须抬到 16 并成功");
        ID::Memory::ID_AlignedFree(zero_alignment_block);

        const ID::size_t logs_before_invalid = g_log_count;
        Check(ID::Memory::ID_AlignedAlloc(32, 24) == nullptr,
              "memory.alloc.alignment_24", "alignment 24 不是 2 的幂，必须返回 nullptr");
        Check(g_log_count > logs_before_invalid && g_last_log.level == ID::Log::Level::Error,
              "memory.alloc.error_logged", "非法对齐必须发出一条 Error 级内核日志");
        Check(ID::Memory::ID_AlignedAlloc(32, 48) == nullptr,
              "memory.alloc.alignment_48", "alignment 48 不是 2 的幂，必须返回 nullptr");
        Check(ID::Memory::ID_AlignedAlloc(static_cast<ID::size_t>(-1), 16) == nullptr,
              "memory.alloc.size_overflow", "极大 size 必须返回 nullptr 而不是回绕");
        Check(ID::Memory::ID_AlignedAlloc(static_cast<ID::size_t>(-1) - 8, 16) == nullptr,
              "memory.alloc.size_rounding", "size 按对齐取整溢出必须返回 nullptr");

        ID::Memory::ID_AlignedFree(nullptr);
        Check(true, "memory.free.null_is_noop", "释放 nullptr 必须是 no-op");
        Skip("memory.free.interior_pointer", "契约是破坏空闲链 / 断言 abort，不测");
        Skip("memory.free.double_free", "契约是破坏空闲链，可能 abort，不测");
        Skip("memory.large.magic_collision", "64KB 对齐大对象的页首 magic 误判是已知缺口（id-kernel K1），修复前不测");

        // =================================================================
        // 内存：C4 状态机 / 幂等
        // =================================================================
        ID::Memory::ID_MemoryInit();
        void* after_reinit_block = ID::Memory::ID_AlignedAlloc(128, 16);
        Check(after_reinit_block != nullptr, "memory.init.idempotent", "重复 ID_MemoryInit 之后分配仍然可用");
        ID::Memory::ID_AlignedFree(after_reinit_block);

        ID::Memory::ID_MemoryShutdown();
        void* after_shutdown_block = ID::Memory::ID_AlignedAlloc(128, 16);
        Check(after_shutdown_block != nullptr, "memory.shutdown.allocatable",
              "当前契约里 shutdown 只记日志，之后分配仍然可用");
        ID::Memory::ID_AlignedFree(after_shutdown_block);
        ID::Memory::ID_MemoryShutdown();
        Check(true, "memory.shutdown.idempotent", "重复 shutdown 必须是 no-op");

        // =================================================================
        // 内存：C1 统计计数器方向正确
        // =================================================================
        const ID::Memory::MemoryStats stats = ID::Memory::ID_GetMemoryStats();
        Check(stats.active_slab_count > 0, "memory.stats.active_slab", "已分配小对象后 active_slab_count 必须大于 0");
        Check(stats.total_reserved_bytes >= 64 * 1024, "memory.stats.reserved_bytes", "至少应映射一页 64KB");

        const ID::size_t large_bytes_before = stats.large_allocation_bytes;
        void* stats_block = ID::Memory::ID_AlignedAlloc(64 * 1024, 64);
        const ID::size_t large_bytes_during = ID::Memory::ID_GetMemoryStats().large_allocation_bytes;
        Check(stats_block != nullptr && large_bytes_during >= large_bytes_before + 64 * 1024,
              "memory.stats.large_bytes_grow", "大对象分配必须让 large_allocation_bytes 增长");
        ID::Memory::ID_AlignedFree(stats_block);
        Check(ID::Memory::ID_GetMemoryStats().large_allocation_bytes == large_bytes_before,
              "memory.stats.large_bytes_restore", "大对象释放后计数必须回到分配前");

        // =================================================================
        // 内存：C6 释放路径不得覆盖块尾哨兵
        // =================================================================
        {
            constexpr ID::size_t kGuardSize = 64;
            auto* guard_block = static_cast<ID::uint8_t*>(ID::Memory::ID_AlignedAlloc(kGuardSize, 16));
            bool guard_ok = guard_block != nullptr;
            if (guard_ok)
            {
                std::memset(guard_block, 0xAB, kGuardSize);
                ID::Memory::ID_AlignedFree(guard_block);

                auto* reused_block = static_cast<ID::uint8_t*>(ID::Memory::ID_AlignedAlloc(kGuardSize, 16));
                // 同线程释放是 LIFO 头插，下一次同档位分配必须拿回同一块；
                // 空闲链节点只占用块的前 8 字节，块尾哨兵必须仍然保留
                guard_ok = reused_block == guard_block && reused_block[kGuardSize - 1] == 0xAB;
                ID::Memory::ID_AlignedFree(reused_block);
            }
            Check(guard_ok, "memory.free.preserves_tail", "释放后再分配同档位必须拿回同一块且块尾哨兵未变");
        }

        // =================================================================
        // 内存：C7 数据完整性（Realloc）
        // =================================================================
        {
            constexpr ID::size_t kOldSize = 100;
            constexpr ID::size_t kNewSize = 9000;
            auto* original = static_cast<ID::uint8_t*>(ID::Memory::ID_AlignedAlloc(kOldSize, 16));
            bool preserved = original != nullptr;
            if (preserved)
            {
                for (ID::size_t i = 0; i < kOldSize; ++i)
                {
                    original[i] = static_cast<ID::uint8_t>(i * 7 + 3);
                }

                auto* grown = static_cast<ID::uint8_t*>(ID::Memory::ID_AlignedRealloc(original, kNewSize, 16));
                preserved = grown != nullptr;
                if (grown != nullptr)
                {
                    for (ID::size_t i = 0; i < kOldSize; ++i)
                    {
                        if (grown[i] != static_cast<ID::uint8_t>(i * 7 + 3))
                        {
                            preserved = false;
                            break;
                        }
                    }
                    ID::Memory::ID_AlignedFree(grown);
                }
                else
                {
                    // 契约：realloc 失败时原指针仍然有效，调用方继续拥有它
                    ID::Memory::ID_AlignedFree(original);
                }
            }
            Check(preserved, "memory.realloc.preserves_prefix", "扩容到 9000B 后原 100B 内容必须保持不变");
        }

        {
            auto* in_place = static_cast<ID::uint8_t*>(ID::Memory::ID_AlignedAlloc(20, 16));   // 32B 档
            bool in_place_ok = false;
            if (in_place != nullptr)
            {
                in_place[0]  = 0xC3;
                void* same   = ID::Memory::ID_AlignedRealloc(in_place, 24, 16);
                in_place_ok  = same == in_place && static_cast<ID::uint8_t*>(same)[0] == 0xC3;
                ID::Memory::ID_AlignedFree(same);
            }
            Check(in_place_ok, "memory.realloc.in_place", "新尺寸不超过原块容量时必须原地返回同一指针");
        }

        {
            void* allocated = ID::Memory::ID_AlignedRealloc(nullptr, 64, 32);
            Check(allocated != nullptr && (reinterpret_cast<ID::uintptr_t>(allocated) & 31u) == 0,
                  "memory.realloc.null_allocates", "realloc(nullptr, 64, 32) 必须等价于一次新分配");
            Check(ID::Memory::ID_AlignedRealloc(allocated, 0, 32) == nullptr,
                  "memory.realloc.zero_size_frees", "new_size 为 0 时必须释放并返回 nullptr");
        }

        {
            void* kept_block = ID::Memory::ID_AlignedAlloc(64, 16);
            bool  kept_ok    = kept_block != nullptr;
            if (kept_ok)
            {
                static_cast<ID::uint8_t*>(kept_block)[0] = 0x3C;
                const bool rejected = ID::Memory::ID_AlignedRealloc(kept_block, 128, 24) == nullptr;
                kept_ok = rejected && static_cast<ID::uint8_t*>(kept_block)[0] == 0x3C;
                ID::Memory::ID_AlignedFree(kept_block);
            }
            Check(kept_ok, "memory.realloc.invalid_alignment", "非 2 的幂 alignment 必须失败且不改动原块");
        }

        // =================================================================
        // 内存：TLS 帧 Arena 契约
        // =================================================================
        {
            ID::LinearArena& frame_arena = ID::Memory::get_thread_frame_arena();
            Check(frame_arena.is_initialized(), "memory.frame_arena.initialized",
                  "首次取用时帧 Arena 必须已绑定缓冲区");
            Check(&ID::Memory::get_thread_frame_arena() == &frame_arena, "memory.frame_arena.stable",
                  "同一线程两次取用必须是同一个实例");
            void* frame_block = frame_arena.allocate(256, 16);
            Check(frame_block != nullptr && (reinterpret_cast<ID::uintptr_t>(frame_block) & 15u) == 0,
                  "memory.frame_arena.allocate", "帧 Arena 分配必须成功且满足对齐");
            Check(frame_arena.capacity() == ID::Memory::kFrameArenaSize,
                  "memory.frame_arena.capacity", "帧 Arena 容量必须是 kFrameArenaSize");
            Check(frame_arena.allocate(ID::Memory::kFrameArenaSize + 1, 16) == nullptr,
                  "memory.frame_arena.over_capacity", "超过 4MB 的请求必须返回 nullptr");
        }

        // =================================================================
        // 内存：C5 并发契约（跨线程释放 -> remote_free_list -> owner 收割）
        // =================================================================
        {
            constexpr ID::size_t kRemoteBlockCount = 8;
            constexpr ID::size_t kRemoteBlockSize  = 96;
            void* remote_blocks[kRemoteBlockCount] = {};

            bool remote_alloc_ok = true;
            for (ID::size_t i = 0; i < kRemoteBlockCount; ++i)
            {
                remote_blocks[i] = ID::Memory::ID_AlignedAlloc(kRemoteBlockSize, 16);
                remote_alloc_ok  = remote_alloc_ok && remote_blocks[i] != nullptr;
            }

            std::thread releaser([&remote_blocks]() noexcept
            {
                for (ID::size_t i = 0; i < kRemoteBlockCount; ++i)
                {
                    ID::Memory::ID_AlignedFree(remote_blocks[i]);
                }
            });
            releaser.join();

            // 96B 档位一页 681 块：继续分配必然先耗尽当前页，再走慢路径收割 remote_free_list
            constexpr ID::size_t kDrainLimit = 4096;
            void*      drained[kDrainLimit]  = {};
            ID::size_t drained_count         = 0;
            ID::size_t reclaimed             = 0;
            for (ID::size_t i = 0; i < kDrainLimit && reclaimed < kRemoteBlockCount; ++i)
            {
                void* block_from_slab = ID::Memory::ID_AlignedAlloc(kRemoteBlockSize, 16);
                if (block_from_slab == nullptr)
                {
                    break;
                }
                drained[drained_count++] = block_from_slab;
                for (ID::size_t k = 0; k < kRemoteBlockCount; ++k)
                {
                    if (block_from_slab == remote_blocks[k])
                    {
                        ++reclaimed;
                        remote_blocks[k] = nullptr;
                        break;
                    }
                }
            }
            Check(remote_alloc_ok && reclaimed == kRemoteBlockCount,
                  "memory.thread.remote_harvest", "跨线程释放的 8 块必须全部被 owner 线程收割回来");

            for (ID::size_t i = 0; i < drained_count; ++i)
            {
                ID::Memory::ID_AlignedFree(drained[i]);
            }
        }

        {
            const ID::uintptr_t main_thread_tag = ID::Memory::get_current_thread_tag();
            ID::uintptr_t       child_thread_tag = 0;
            bool                child_ok         = false;

            std::thread worker([&child_thread_tag, &child_ok]() noexcept
            {
                child_thread_tag = ID::Memory::get_current_thread_tag();
                void* small_block   = ID::Memory::ID_AlignedAlloc(48, 16);
                void* aligned_block = ID::Memory::ID_AlignedAlloc(1024, 64);
                child_ok = small_block != nullptr
                        && aligned_block != nullptr
                        && (reinterpret_cast<ID::uintptr_t>(aligned_block) & 63u) == 0;
                if (small_block != nullptr)
                {
                    std::memset(small_block, 0x5A, 48);
                }
                ID::Memory::ID_AlignedFree(small_block);
                ID::Memory::ID_AlignedFree(aligned_block);
            });
            worker.join();

            Check(child_ok, "memory.thread.child_alloc_free", "子线程独立分配/释放必须成功");
            Check(child_thread_tag != 0 && child_thread_tag != main_thread_tag,
                  "memory.thread.tag_distinct", "不同线程的 thread tag 必须不同");
        }

        // =================================================================
        // 插件运行期（PluginRuntime）：状态机、四阶段顺序、失败清理、槽发布/注销所有权
        //   旧的运行期登记（register_plugin(descriptor*) / initialize_all / shutdown_all）与
        //   运行期 OR 拓扑已按 P4 退役，本节全部判定走 start / shutdown / publish_service /
        //   revoke_service 与 get_service；依赖与后端选择只认编译期唯一绑定结论。
        // =================================================================
        ID::PluginRuntime& runtime = ID::PluginRuntime::instance();
        runtime.shutdown();                 // Idle 的 shutdown 是幂等无操作，从干净状态开始
        reset_case_state();

        Check(runtime.state() == ID::PluginRuntimeState::Idle && all_slots_empty(),
              "runtime.domain.idle_before", "用例集进入前必须是 Idle 且 64 槽全空");

        // ---------- 正常四阶段：计划给定顺序被逐字执行 ----------
        {
            reset_case_state();
            const ID::PluginRuntimeDiagnostic started = runtime.start(kPlanChain);
            Check(started.is_ok() && started.state == ID::PluginRuntimeState::Ready
               && runtime.state() == ID::PluginRuntimeState::Ready,
                  "runtime.lifecycle.start_ok", "合法计划必须进入 Ready");
            check_lifecycle(kSequenceChainStart, "runtime.order.follows_plan");

            const ID::PluginRuntimeDiagnostic stopped = runtime.shutdown();
            Check(stopped.is_ok() && runtime.state() == ID::PluginRuntimeState::Idle,
                  "runtime.lifecycle.shutdown_ok", "关闭必须回到 Idle");
            check_lifecycle(kSequenceChainFull, "runtime.lifecycle.sequence");
            Check(all_slots_empty(), "runtime.shutdown.clears_slots", "关闭结束后 64 个槽必须全部为空");
        }

        {
            reset_case_state();
            const bool restart_ok = runtime.start(kPlanChain).is_ok();
            const ID::PluginRuntimeDiagnostic stopped = runtime.shutdown();
            Check(restart_ok && stopped.is_ok() && runtime.state() == ID::PluginRuntimeState::Idle,
                  "runtime.lifecycle.retry", "同一计划关闭后必须可以再次启动并关闭");
            check_lifecycle(kSequenceChainFull, "runtime.lifecycle.retry_order");
        }

        {
            reset_case_state();
            const ID::PluginRuntimeDiagnostic started = runtime.start(kPlanDiamond);
            const ID::PluginRuntimeDiagnostic stopped = runtime.shutdown();
            Check(started.is_ok() && stopped.is_ok(),
                  "runtime.diamond.start_ok", "菱形拓扑的合法计划必须完整走完四阶段");
            check_lifecycle(kSequenceDiamondFull, "runtime.diamond.order");
        }

        {
            reset_case_state();
            const ID::PluginRuntimeDiagnostic started = runtime.start(kPlanZero);
            const ID::PluginRuntimeDiagnostic stopped = runtime.shutdown();
            Check(started.is_ok() && started.state == ID::PluginRuntimeState::Ready,
                  "runtime.zero_plugins.start", "0 插件计划必须直接进入 Ready");
            Check(stopped.is_ok() && runtime.state() == ID::PluginRuntimeState::Idle && g_lifecycle_length == 0,
                  "runtime.zero_plugins.shutdown", "0 插件关闭回 Idle 且不执行任何回调");
        }

        // ---------- 容量边界：128 接受 / 129 先按计数拒绝 ----------
        {
            reset_case_state();
            const ID::PluginRuntimeDiagnostic started = runtime.start(kPlanCapacity);
            Check(started.is_ok() && runtime.state() == ID::PluginRuntimeState::Ready,
                  "runtime.capacity.128", "128 个记录的计划必须被接受");
            const ID::PluginRuntimeDiagnostic stopped = runtime.shutdown();
            Check(stopped.is_ok() && runtime.state() == ID::PluginRuntimeState::Idle,
                  "runtime.capacity.128_close", "128 插件计划的关闭必须回到 Idle");

            // 数组指针全部为空：若实现先索引再校验计数，这里会读到空指针而不是返回错误
            ID::PluginRuntimePlanView overflow{};
            overflow.plugin_count = static_cast<ID::uint16_t>(kCapacityPluginCount + 1);
            const ID::PluginRuntimeDiagnostic rejected = runtime.start(overflow);
            Check(rejected.code == ID::PluginRuntimeError::PlanTooManyPlugins
               && runtime.state() == ID::PluginRuntimeState::Idle,
                  "runtime.capacity.129", "129 个插件必须先按计数拒绝，不索引空数组");
        }

        // ---------- init 失败的清理：失败插件自身 shutdown + 已注册全集 unload + 可重试 ----------
        {
            reset_case_state();
            g_init_fail_role = 'B';
            const ID::PluginRuntimeDiagnostic failed = runtime.start(kPlanChain);
            Check(failed.code == ID::PluginRuntimeError::InitFailed
               && failed.plugin == 1
               && failed.plugin_id == ID::hash_fnv_1a64("Bench.Kernel.B")
               && runtime.state() == ID::PluginRuntimeState::Idle,
                  "runtime.init_fail.middle", "中间插件 init 失败必须返回 InitFailed 并回滚到 Idle");
            check_lifecycle("RARBRCIAIBSBSAUCUBUA", "runtime.init_fail.rollback_order");
            Check(all_slots_empty(), "runtime.init_fail.slots_cleared", "回滚后不得残留任何槽位");

            g_init_fail_role = '\0';
            reset_lifecycle();
            const ID::PluginRuntimeDiagnostic retried = runtime.start(kPlanChain);
            Check(retried.is_ok(), "runtime.init_fail.retry", "修正条件后同一计划必须能再次启动");
            static_cast<void>(runtime.shutdown());
            check_lifecycle(kSequenceChainFull, "runtime.init_fail.retry_order");
        }

        // ---------- 状态机：重复 start 与回调内重入 ----------
        {
            reset_case_state();
            static_cast<void>(runtime.start(kPlanChain));
            reset_lifecycle();                       // 只看重复 start 有没有又跑一遍回调
            const ID::PluginRuntimeDiagnostic second = runtime.start(kPlanChain);
            Check(second.code == ID::PluginRuntimeError::AlreadyStarted
               && runtime.state() == ID::PluginRuntimeState::Ready
               && g_lifecycle_length == 0,
                  "runtime.state.already_started", "Ready 重复 start 必须返回 AlreadyStarted 且不执行回调");
            static_cast<void>(runtime.shutdown());
        }

        {
            reset_case_state();
            g_reentry_probe = true;
            const ID::PluginRuntimeDiagnostic started = runtime.start(kPlanChain);
            Check(started.is_ok() && g_reentry_code == ID::PluginRuntimeError::Busy
               && runtime.state() == ID::PluginRuntimeState::Ready,
                  "runtime.state.reentry_busy", "回调内重入 shutdown 必须返回 Busy 且不中断启动");
            const ID::PluginRuntimeDiagnostic stopped = runtime.shutdown();
            Check(stopped.is_ok(), "runtime.state.reentry_shutdown", "重入被拒后正常关闭必须成功");
            check_lifecycle(kSequenceChainFull, "runtime.state.reentry_flow");
        }

        // ---------- 服务槽：发布、读取、注销全部按计划所有权 ----------
        {
            reset_case_state();
            g_slot_action = SlotAction::PublishBoth;
            const ID::PluginRuntimeDiagnostic started = runtime.start(kPlanSlot);
            Check(started.is_ok() && g_action_code == ID::PluginRuntimeError::None
               && g_action_code_2 == ID::PluginRuntimeError::None,
                  "runtime.slot.publish_ok", "计划分配给本插件的两个槽必须发布成功");

            const FakeApiTable* math_table    = runtime.get_service<FakeApiTable>(ID::ServiceSlot::Math);
            const FakeApiTable* physics_table = runtime.get_service<FakeApiTable>(ID::ServiceSlot::Physics);
            Check(math_table == &kApiTableOne && math_table->magic == kApiTableOne.magic,
                  "runtime.slot.publish_and_read", "固定槽位必须取回发布时的同一张表");
            Check(physics_table == &kApiTableTwo && physics_table->magic == kApiTableTwo.magic,
                  "runtime.slot.two_slots", "同一插件的两个槽互不影响");
            Check(runtime.get_service<FakeApiTable>(ID::ServiceSlot::ECS) == nullptr,
                  "runtime.slot.unowned_empty", "不在计划里的槽必须为空");

            const ID::PluginRuntimeDiagnostic stopped = runtime.shutdown();
            Check(stopped.is_ok() && all_slots_empty(),
                  "runtime.slot.revoke_on_unload", "on_unload 注销自己的槽，关闭后 64 槽全空");
        }

        {
            reset_case_state();
            g_slot_action = SlotAction::PublishTwice;
            const ID::PluginRuntimeDiagnostic duplicated = runtime.start(kPlanSlot);
            Check(g_action_code == ID::PluginRuntimeError::None
               && g_action_code_2 == ID::PluginRuntimeError::ServiceDuplicatePublish,
                  "runtime.slot.duplicate_publish", "同一槽第二次发布必须被拒绝，不得后写覆盖");
            Check(duplicated.code == ID::PluginRuntimeError::ServiceDuplicatePublish
               && runtime.state() == ID::PluginRuntimeState::Idle && all_slots_empty(),
                  "runtime.slot.duplicate_rollback", "发布违约必须让 start 回滚到 Idle 并清空槽位");
        }

        {
            reset_case_state();
            g_slot_action = SlotAction::PublishNullThenBoth;
            const ID::PluginRuntimeDiagnostic null_publish = runtime.start(kPlanSlot);
            Check(g_action_code == ID::PluginRuntimeError::ServiceTableNull,
                  "runtime.slot.null_publish", "publish(nullptr) 必须被拒：注销只能走 revoke_service");
            Check(null_publish.code == ID::PluginRuntimeError::ServiceTableNull
               && runtime.state() == ID::PluginRuntimeState::Idle,
                  "runtime.slot.null_rollback", "空表发布违约同样让 start 回滚到 Idle");
        }

        {
            reset_case_state();
            g_slot_action = SlotAction::PublishOutOfRange;
            const ID::PluginRuntimeDiagnostic out_of_range = runtime.start(kPlanSlot);
            Check(g_action_code == ID::PluginRuntimeError::ServiceSlotOutOfRange
               && out_of_range.code == ID::PluginRuntimeError::ServiceSlotOutOfRange
               && out_of_range.slot == 64
               && runtime.state() == ID::PluginRuntimeState::Idle,
                  "runtime.slot.out_of_range", "槽 64 必须在索引数组之前被拒绝");
        }

        {
            // 两个记录：A 拥有槽 2/5，B 不拥有任何槽；让 B 去写 A 的槽
            reset_case_state();
            const ID::PluginRuntimeRecord foreign_records[2] = { kRecordA, kRecordB };
            ID::PluginRuntimePlanView    foreign_plan        = kPlanSlot;
            foreign_plan.plugin_count = 2;
            foreign_plan.records      = foreign_records;
            foreign_plan.topo_order   = kPairTopo;

            g_slot_actor  = 'B';
            g_slot_action = SlotAction::PublishForeign;
            const ID::PluginRuntimeDiagnostic foreign = runtime.start(foreign_plan);
            Check(g_action_code == ID::PluginRuntimeError::ServiceOwnerMismatch
               && foreign.code == ID::PluginRuntimeError::ServiceOwnerMismatch
               && foreign.plugin == 1 && foreign.slot == static_cast<int16_t>(kMathSlot)
               && runtime.state() == ID::PluginRuntimeState::Idle,
                  "runtime.slot.owner_mismatch", "写别人的槽必须被拒绝，不允许覆盖他人服务");

            reset_case_state();
            g_slot_action = SlotAction::PublishBoth;          // 合法 owner 自己发布，B 不碰别人的槽
            const ID::PluginRuntimeDiagnostic legal = runtime.start(foreign_plan);
            Check(legal.is_ok(), "runtime.slot.owner_control", "同一计划下不越权发布必须进入 Ready");
            static_cast<void>(runtime.shutdown());
        }

        {
            reset_case_state();
            g_slot_action = SlotAction::PublishBoth;
            static_cast<void>(runtime.start(kPlanSlot));
            const ID::PluginRuntimeDiagnostic publish_ready = runtime.publish_service(kMathSlot, &kApiTableTwo);
            const ID::PluginRuntimeDiagnostic revoke_ready  = runtime.revoke_service(kMathSlot);
            Check(publish_ready.code == ID::PluginRuntimeError::ServicePublishRejected
               && revoke_ready.code == ID::PluginRuntimeError::ServiceRevokeRejected,
                  "runtime.slot.ready_write_rejected", "Ready 期间槽表只读：发布与注销都必须被拒绝");
            Check(runtime.get_service<FakeApiTable>(ID::ServiceSlot::Math) == &kApiTableOne,
                  "runtime.slot.ready_preserved", "两次拒绝之后槽位仍必须指向原表");
            const ID::PluginRuntimeDiagnostic stopped = runtime.shutdown();
            Check(stopped.is_ok() && all_slots_empty(),
                  "runtime.slot.ready_shutdown", "关闭后 64 槽全空");
        }

        // ---------- 计划预检：身份唯一、槽位唯一、接口修订必须显式 ----------
        {
            reset_case_state();
            ID::PluginRuntimeRecord duplicate_records[2] = { kRecordA, kRecordB };
            duplicate_records[1].plugin_id = duplicate_records[0].plugin_id;
            ID::PluginRuntimePlanView duplicate_plan{};
            duplicate_plan.plugin_count = 2;
            duplicate_plan.records      = duplicate_records;
            duplicate_plan.topo_order   = kPairTopo;
            duplicate_plan.slot_owner   = kOwnersEmpty.cells;
            const ID::PluginRuntimeDiagnostic rejected = runtime.start(duplicate_plan);
            Check(rejected.code == ID::PluginRuntimeError::PlanInvalidRecord && rejected.plugin == 1
               && runtime.state() == ID::PluginRuntimeState::Idle && g_lifecycle_length == 0,
                  "runtime.plan.duplicate_identity", "产品内插件身份必须唯一，重复身份的计划必须被拒");
        }

        {
            reset_case_state();
            const ID::PluginRuntimeProvision shared_slot_provisions[2] = {
                { .capability = "bench.kernel.math",    .api_revision = 1, .plugin_index = 0,
                  .slot = static_cast<int16_t>(kMathSlot) },
                { .capability = "bench.kernel.physics", .api_revision = 1, .plugin_index = 1,
                  .slot = static_cast<int16_t>(kMathSlot) }
            };
            SlotOwnerTable shared_owners{};
            shared_owners.set(kMathSlot, 0);
            ID::PluginRuntimePlanView shared_plan{};
            shared_plan.plugin_count    = 2;
            shared_plan.provision_count = 2;
            shared_plan.records         = kChainRecords;      // 任取两个记录（C,B）即可
            shared_plan.topo_order      = kPairTopo;
            shared_plan.provisions      = shared_slot_provisions;
            shared_plan.slot_owner      = shared_owners.cells;
            const ID::PluginRuntimeDiagnostic rejected = runtime.start(shared_plan);
            Check(rejected.code == ID::PluginRuntimeError::PlanInvalidSlotOwner
               && rejected.slot == static_cast<int16_t>(kMathSlot)
               && runtime.state() == ID::PluginRuntimeState::Idle && g_lifecycle_length == 0,
                  "runtime.plan.two_slot_providers", "同一槽两个提供者必须被拒：产品级服务槽唯一");
        }

        {
            reset_case_state();
            const ID::PluginRuntimeProvision zero_revision_provisions[1] = {
                { .capability = "bench.kernel.math", .api_revision = 0, .plugin_index = 0,
                  .slot = static_cast<int16_t>(kMathSlot) }
            };
            SlotOwnerTable zero_revision_owners{};
            zero_revision_owners.set(kMathSlot, 0);
            ID::PluginRuntimePlanView zero_revision_plan{};
            zero_revision_plan.plugin_count    = 1;
            zero_revision_plan.provision_count = 1;
            zero_revision_plan.records         = kSlotRecords;
            zero_revision_plan.topo_order      = kSlotTopo;
            zero_revision_plan.provisions      = zero_revision_provisions;
            zero_revision_plan.slot_owner      = zero_revision_owners.cells;
            const ID::PluginRuntimeDiagnostic rejected = runtime.start(zero_revision_plan);
            Check(rejected.code == ID::PluginRuntimeError::PlanInvalidProvision
               && runtime.state() == ID::PluginRuntimeState::Idle && g_lifecycle_length == 0,
                  "runtime.plan.revision_zero", "接口修订必须显式给出：0 不能冒充有效修订");
        }

        // =================================================================
        // 编译期唯一绑定：替代旧“任一白名单候选即可满足”的 OR 语义
        // =================================================================
        {
            reset_case_state();
            Check(kPlanAmbiguous.diagnostic.code == ID::PluginCompileTime::PluginError::AmbiguousProvider
               && kPlanAmbiguous.diagnostic.phase == ID::PluginCompileTime::AssemblyPhase::Selection
               && kPlanAmbiguous.candidate_match_count == 2,
                  "assembly.binding.ambiguous", "两个候选都选中必须报歧义：不挑第一个、不挑最新");
            Check(kPlanOldAndNew.diagnostic.code == ID::PluginCompileTime::PluginError::AmbiguousProvider,
                  "assembly.binding.old_new", "一旧一新同时启用仍是歧义：名称唯一先于版本");
            Check(kPlanMissing.diagnostic.code == ID::PluginCompileTime::PluginError::MissingProvider
               && kPlanMissing.candidate_match_count == 0,
                  "assembly.binding.missing", "选中集合里 0 个候选必须报缺提供者");
            Check(kPlanCycle.diagnostic.code == ID::PluginCompileTime::PluginError::Cycle
               && kPlanCycle.diagnostic.phase == ID::PluginCompileTime::AssemblyPhase::Graph,
                  "assembly.graph.cycle", "选中后的固定图成环必须在编译期被拒绝");
        }

        Skip("runtime.slot.concurrent_write",
             "无并发写契约：publish/revoke 只允许在宿主控制线程的生命周期回调内（PluginRuntime.hpp 第 5 条）");

        Check(runtime.state() == ID::PluginRuntimeState::Idle && all_slots_empty(),
              "runtime.domain.idle_after", "用例集结束后必须是 Idle 且 64 槽全空");

        // =================================================================
        // 自举日志：派发契约与还原
        // =================================================================
        const ID::size_t log_count_before = g_log_count;
        ID::Log::BootstrapLog::instance().report(ID::Log::Level::Warn, "ID.Test", "benchmark log probe");
        Check(g_log_count == log_count_before + 1
           && g_last_log.level == ID::Log::Level::Warn
           && std::strcmp(g_last_log.category, "ID.Test") == 0
           && std::strcmp(g_last_log.message, "benchmark log probe") == 0,
              "log.report_dispatch", "report 必须把 level/category/message 原样交给当前 sink");

        Check(g_log_count > 0, "log.capture_observed", "整轮测试期间内核确实产生过日志");

        // 先把 shutdown 的日志也交给捕获 sink，否则它会用默认 sink 写 stderr，与 report.txt 分叉
        ID::Memory::ID_MemoryShutdown();

        ID::Log::BootstrapLog::instance().set_sink(&ID::Log::default_bootstrap_sink);
        Check(ID::Log::BootstrapLog::instance().get_sink() == &ID::Log::default_bootstrap_sink,
              "log.restore_default", "测试结束必须把 sink 还原为 default_bootstrap_sink");
    }
} // namespace ID::Bench
