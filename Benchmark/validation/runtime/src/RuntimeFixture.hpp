#pragma once

#include <cstddef>
#include <cstdint>

#include "Common/Core.hpp"
#include "Kernel/CoreTypes.hpp"
#include "Kernel/Plugin.hpp"
#include "Kernel/PluginRuntime.hpp"

/**
 *  运行期 PluginRuntime 校验夹具（P3，仅测试用途，不属于生产插件）
 *  1. 全部插件角色都在测试内定义，不使用 IDPlugin 里的任何真实插件；每个角色只有四个
 *     noexcept 回调，回调只做两件事：按"角色.阶段"追加调用序列，以及按当前模式调用
 *     publish_service / revoke_service / get_service；
 *  2. 计划视图与它指向的记录、拓扑序、provides、slot_owner 全部是 inline constexpr，
 *     具有静态存储期，符合 PluginRuntime 对计划数据生命周期与 ABI 前置声明的要求；
 *     需要"只改一处"的边界用例使用 scratch 副本（运行期赋值静态存储对象），不改常量本身；
 *  3. 断言只做真实条件比较：枚举相等、序列字符串相等、地址相等、函数返回值相等。
 *     detail 里写出期望值与实际值，不用固定文案冒充判定；
 *  4. 每个失败用例后面都紧跟同环境的合法对照（同一计划、同一夹具，只把违约开关关掉）。
 */
namespace ID::Bench::Runtime
{
    // 调用序列缓冲容量：本域最多 4 插件 × 4 阶段，最长序列约 130 字节，512 有充足余量；
    // 溢出后不再写入，序列比较会因为截断而失败，不会被当成通过
    inline constexpr size_t kSequenceCapacity = 512;

    // on_init 的失败位置；None 是合法对照，三个取值分别对应拓扑序里的首/中/末
    enum class InitFailMode : uint8_t
    {
        None = 0,
        First,
        Middle,
        Last
    };

    // 上游 U 在 on_register 的行为模式；Ok 是合法对照，其余四种各触发一类发布违约
    enum class RegisterMode : uint8_t
    {
        Ok = 0,
        Missing,
        WrongOwner,
        Duplicate,
        OutOfRange
    };

    // ---- 上游公开 API（纯测试内定义：两张函数表 + 三个实现函数） ----

    int32_t test_api_add(int32_t lhs, int32_t rhs) noexcept;
    int32_t test_api_square(int32_t value) noexcept;
    int32_t test_api_scale(int32_t value, int32_t factor) noexcept;

    struct TestMathApi
    {
        int32_t (*add)(int32_t lhs, int32_t rhs) noexcept;      // 求和
        int32_t (*square)(int32_t value) noexcept;              // 平方
    };

    struct TestAuxApi
    {
        int32_t (*scale)(int32_t value, int32_t factor) noexcept;   // 按倍数放大
    };

    inline constexpr TestMathApi kMathTable{ &test_api_add, &test_api_square };
    inline constexpr TestAuxApi  kAuxTable{ &test_api_scale };

    // ---- 四个只记录序列的角色（组 1/2/3/5） ----

    void A_on_register() noexcept;
    bool A_on_init() noexcept;
    void A_on_shutdown() noexcept;
    void A_on_unload() noexcept;

    void B_on_register() noexcept;
    bool B_on_init() noexcept;
    void B_on_shutdown() noexcept;
    void B_on_unload() noexcept;

    void C_on_register() noexcept;
    bool C_on_init() noexcept;
    void C_on_shutdown() noexcept;
    void C_on_unload() noexcept;

    void D_on_register() noexcept;
    bool D_on_init() noexcept;
    void D_on_shutdown() noexcept;
    void D_on_unload() noexcept;

    // ---- 服务角色（组 4/6/7）：U 提供槽 2，V 提供槽 3 并在 on_init 调用上游 API ----

    void U_on_register() noexcept;
    bool U_on_init() noexcept;
    void U_on_shutdown() noexcept;
    void U_on_unload() noexcept;

    void V_on_register() noexcept;
    bool V_on_init() noexcept;
    void V_on_shutdown() noexcept;
    void V_on_unload() noexcept;

    // ---- 槽位边界角色（组 6）：E 提供槽 63 ----

    void E_on_register() noexcept;
    bool E_on_init() noexcept;
    void E_on_shutdown() noexcept;
    void E_on_unload() noexcept;

    // ---- 无槽位能力角色（组 9）：N 的 provides 没有运行期函数表，注册期不发任何槽 ----

    void N_on_register() noexcept;
    bool N_on_init() noexcept;
    void N_on_shutdown() noexcept;
    void N_on_unload() noexcept;

    inline constexpr PluginRuntimeCallbacks kCallbacksA{ &A_on_register, &A_on_init, &A_on_shutdown, &A_on_unload };
    inline constexpr PluginRuntimeCallbacks kCallbacksB{ &B_on_register, &B_on_init, &B_on_shutdown, &B_on_unload };
    inline constexpr PluginRuntimeCallbacks kCallbacksC{ &C_on_register, &C_on_init, &C_on_shutdown, &C_on_unload };
    inline constexpr PluginRuntimeCallbacks kCallbacksD{ &D_on_register, &D_on_init, &D_on_shutdown, &D_on_unload };
    inline constexpr PluginRuntimeCallbacks kCallbacksU{ &U_on_register, &U_on_init, &U_on_shutdown, &U_on_unload };
    inline constexpr PluginRuntimeCallbacks kCallbacksV{ &V_on_register, &V_on_init, &V_on_shutdown, &V_on_unload };
    inline constexpr PluginRuntimeCallbacks kCallbacksE{ &E_on_register, &E_on_init, &E_on_shutdown, &E_on_unload };
    inline constexpr PluginRuntimeCallbacks kCallbacksN{ &N_on_register, &N_on_init, &N_on_shutdown, &N_on_unload };

    inline constexpr PluginRuntimeRecord kRecordA{
        .name      = "RT.A",
        .plugin_id = hash_fnv_1a64("RT.A"),
        .callbacks = kCallbacksA
    };
    inline constexpr PluginRuntimeRecord kRecordB{
        .name      = "RT.B",
        .plugin_id = hash_fnv_1a64("RT.B"),
        .callbacks = kCallbacksB
    };
    inline constexpr PluginRuntimeRecord kRecordC{
        .name      = "RT.C",
        .plugin_id = hash_fnv_1a64("RT.C"),
        .callbacks = kCallbacksC
    };
    inline constexpr PluginRuntimeRecord kRecordD{
        .name      = "RT.D",
        .plugin_id = hash_fnv_1a64("RT.D"),
        .callbacks = kCallbacksD
    };
    inline constexpr PluginRuntimeRecord kRecordU{
        .name      = "RT.U",
        .plugin_id = hash_fnv_1a64("RT.U"),
        .callbacks = kCallbacksU
    };
    inline constexpr PluginRuntimeRecord kRecordV{
        .name      = "RT.V",
        .plugin_id = hash_fnv_1a64("RT.V"),
        .callbacks = kCallbacksV
    };
    inline constexpr PluginRuntimeRecord kRecordE{
        .name      = "RT.E",
        .plugin_id = hash_fnv_1a64("RT.E"),
        .callbacks = kCallbacksE
    };
    inline constexpr PluginRuntimeRecord kRecordN{
        .name      = "RT.N",
        .plugin_id = hash_fnv_1a64("RT.N"),
        .callbacks = kCallbacksN
    };

    // 槽位 -> owner 表；默认 64 项全部是 PluginCompileTime::kNoIndex（空槽）
    struct SlotOwnerTable
    {
        uint16_t cells[PluginRuntime::kMaxSlots];

        constexpr SlotOwnerTable() noexcept
            : cells{}
        {
            for (size_t slot = 0; slot < PluginRuntime::kMaxSlots; ++slot)
            {
                cells[slot] = PluginCompileTime::kNoIndex;
            }
        }

        constexpr void set(size_t slot, uint16_t owner) noexcept
        {
            cells[slot] = owner;
        }
    };

    // 4 个无服务插件：records 顺序 A,B,C,D，拓扑序 D,B,A,C（故意不是恒等排列，
    // 用来证明 runtime 严格按 topo_order 执行，而不是按 records 顺序）
    inline constexpr PluginRuntimeRecord kRecordsAbcd[4] = { kRecordA, kRecordB, kRecordC, kRecordD };
    inline constexpr uint16_t             kTopoDbac[4]    = { 3, 1, 0, 2 };
    inline constexpr SlotOwnerTable       kOwnersEmpty{};

    inline constexpr PluginRuntimePlanView kPlanAbcd{
        .abi_revision    = kPluginRuntimeAbiRevision,
        .struct_size     = static_cast<uint32_t>(sizeof(PluginRuntimePlanView)),
        .plugin_count    = 4,
        .provision_count = 0,
        .records         = kRecordsAbcd,
        .topo_order      = kTopoDbac,
        .provisions      = nullptr,
        .slot_owner      = kOwnersEmpty.cells
    };

    // 3 个无服务插件：拓扑序 A,B,C（组 3 用它区分首/中/末 init 失败）
    inline constexpr PluginRuntimeRecord kRecordsAbc[3] = { kRecordA, kRecordB, kRecordC };
    inline constexpr uint16_t             kTopoAbc[3]    = { 0, 1, 2 };

    inline constexpr PluginRuntimePlanView kPlanAbc{
        .abi_revision    = kPluginRuntimeAbiRevision,
        .struct_size     = static_cast<uint32_t>(sizeof(PluginRuntimePlanView)),
        .plugin_count    = 3,
        .provision_count = 0,
        .records         = kRecordsAbc,
        .topo_order      = kTopoAbc,
        .provisions      = nullptr,
        .slot_owner      = kOwnersEmpty.cells
    };

    // 零插件：全部指针为空（默认成员初始化给出版本与尺寸）
    inline constexpr PluginRuntimePlanView kPlanZeroNull{};

    // 零插件：显式空视图（指针指向真实静态数组，但计数为 0）
    inline constexpr PluginRuntimePlanView kPlanZeroExplicit{
        .abi_revision    = kPluginRuntimeAbiRevision,
        .struct_size     = static_cast<uint32_t>(sizeof(PluginRuntimePlanView)),
        .plugin_count    = 0,
        .provision_count = 0,
        .records         = kRecordsAbc,
        .topo_order      = kTopoAbc,
        .provisions      = nullptr,
        .slot_owner      = kOwnersEmpty.cells
    };

    // 零插件、但 records 指向真实静态数组：用来把"全空视图"与"仅 records 非空"的差异单独隔离出来，
    // 两者计数都是 0、都没有回调，唯一区别是 records 是否为 nullptr
    inline constexpr PluginRuntimePlanView kPlanZeroNullRecords{
        .abi_revision    = kPluginRuntimeAbiRevision,
        .struct_size     = static_cast<uint32_t>(sizeof(PluginRuntimePlanView)),
        .plugin_count    = 0,
        .provision_count = 0,
        .records         = kRecordsAbc,
        .topo_order      = nullptr,
        .provisions      = nullptr,
        .slot_owner      = nullptr
    };

    // 两个服务插件：U 提供槽 2（Math），V 提供槽 3，拓扑序 U -> V
    inline constexpr PluginRuntimeRecord kRecordsUv[2] = { kRecordU, kRecordV };
    inline constexpr uint16_t             kTopoUv[2]    = { 0, 1 };

    inline constexpr PluginRuntimeProvision kProvisionUv[2] = {
        { .capability = "rt.math", .api_revision = 1, .plugin_index = 0, .slot = 2 },
        { .capability = "rt.aux",  .api_revision = 1, .plugin_index = 1, .slot = 3 }
    };

    constexpr SlotOwnerTable make_slot_owner_uv() noexcept
    {
        SlotOwnerTable table{};
        table.set(2, 0);
        table.set(3, 1);
        return table;
    }

    inline constexpr SlotOwnerTable kOwnersUv = make_slot_owner_uv();

    inline constexpr PluginRuntimePlanView kPlanUv{
        .abi_revision    = kPluginRuntimeAbiRevision,
        .struct_size     = static_cast<uint32_t>(sizeof(PluginRuntimePlanView)),
        .plugin_count    = 2,
        .provision_count = 2,
        .records         = kRecordsUv,
        .topo_order      = kTopoUv,
        .provisions      = kProvisionUv,
        .slot_owner      = kOwnersUv.cells
    };

    // 槽位 63：E 提供槽 63（合法上界），用来区分 63 合法与 64 拒绝
    inline constexpr PluginRuntimeRecord kRecordsSlotE[1] = { kRecordE };
    inline constexpr uint16_t             kTopoSlotE[1]   = { 0 };

    inline constexpr PluginRuntimeProvision kProvisionSlot63[1] = {
        { .capability = "rt.math", .api_revision = 1, .plugin_index = 0, .slot = 63 }
    };

    constexpr SlotOwnerTable make_slot_owner_63() noexcept
    {
        SlotOwnerTable table{};
        table.set(63, 0);
        return table;
    }

    inline constexpr SlotOwnerTable kOwnersSlot63 = make_slot_owner_63();

    inline constexpr PluginRuntimePlanView kPlanSlot63{
        .abi_revision    = kPluginRuntimeAbiRevision,
        .struct_size     = static_cast<uint32_t>(sizeof(PluginRuntimePlanView)),
        .plugin_count    = 1,
        .provision_count = 1,
        .records         = kRecordsSlotE,
        .topo_order      = kTopoSlotE,
        .provisions      = kProvisionSlot63,
        .slot_owner      = kOwnersSlot63.cells
    };

    // 无槽位能力（slot == kNoSlot）：N 的 provides 没有运行期函数表，
    // 按计划书 11.1 / 12.2 注册期不发布任何槽位，runtime 也不能要求它发布
    inline constexpr PluginRuntimeRecord kRecordsNoSlot[1] = { kRecordN };
    inline constexpr uint16_t             kTopoNoSlot[1]   = { 0 };

    inline constexpr PluginRuntimeProvision kProvisionNoSlot[1] = {
        { .capability = "rt.headonly", .api_revision = 1, .plugin_index = 0, .slot = kNoSlot }
    };

    inline constexpr PluginRuntimePlanView kPlanNoSlot{
        .abi_revision    = kPluginRuntimeAbiRevision,
        .struct_size     = static_cast<uint32_t>(sizeof(PluginRuntimePlanView)),
        .plugin_count    = 1,
        .provision_count = 1,
        .records         = kRecordsNoSlot,
        .topo_order      = kTopoNoSlot,
        .provisions      = kProvisionNoSlot,
        .slot_owner      = kOwnersEmpty.cells
    };

    // 混合 provision：N 的无槽位能力 + U 的槽 2。slot_owner 只登记有槽能力那一条，
    // 无槽位能力不得在槽归属表里凭空占位（组 9 的第 3 条断言依据）
    inline constexpr PluginRuntimeRecord kRecordsMixed[2] = { kRecordN, kRecordU };
    inline constexpr uint16_t             kTopoMixed[2]   = { 0, 1 };

    inline constexpr PluginRuntimeProvision kProvisionMixed[2] = {
        { .capability = "rt.headonly", .api_revision = 1, .plugin_index = 0, .slot = kNoSlot },
        { .capability = "rt.math",     .api_revision = 1, .plugin_index = 1, .slot = 2 }
    };

    constexpr SlotOwnerTable make_slot_owner_mixed() noexcept
    {
        SlotOwnerTable table{};
        table.set(2, 1);
        return table;
    }

    inline constexpr SlotOwnerTable kOwnersMixed = make_slot_owner_mixed();

    inline constexpr PluginRuntimePlanView kPlanMixed{
        .abi_revision    = kPluginRuntimeAbiRevision,
        .struct_size     = static_cast<uint32_t>(sizeof(PluginRuntimePlanView)),
        .plugin_count    = 2,
        .provision_count = 2,
        .records         = kRecordsMixed,
        .topo_order      = kTopoMixed,
        .provisions      = kProvisionMixed,
        .slot_owner      = kOwnersMixed.cells
    };

    // 超过 kMaxNameBytes(96) 的名字，用于记录身份的长度边界（末尾仍有 NUL）
    inline constexpr char kLongRecordName[] =
        "RT.LONG.NAME.XX.RT.LONG.NAME.XX.RT.LONG.NAME.XX.RT.LONG.NAME.XX."
        "RT.LONG.NAME.XX.RT.LONG.NAME.XX.RT.LONG.NAME.XX.RT.LONG.NAME.XX."
        "RT.LONG.NAME.XX.";
    static_assert(sizeof(kLongRecordName) - 1 > PluginCompileTime::kMaxNameBytes,
                  "长度边界用例的名字必须超过 kMaxNameBytes");

    // ---- 期望序列（组 1/3/5/6 的精确顺序断言） ----

    // kPlanAbcd 的拓扑序 D,B,A,C：正序 register -> init，逆序 shutdown -> unload
    inline constexpr char kSequenceAbcdStart[] = "D.r,B.r,A.r,C.r,D.i,B.i,A.i,C.i";
    inline constexpr char kSequenceAbcdFull[]  =
        "D.r,B.r,A.r,C.r,D.i,B.i,A.i,C.i,C.s,A.s,B.s,D.s,C.u,A.u,B.u,D.u";

    inline constexpr char kSequenceAbcStart[] = "A.r,B.r,C.r,A.i,B.i,C.i";
    inline constexpr char kSequenceAbcFull[]  = "A.r,B.r,C.r,A.i,B.i,C.i,C.s,B.s,A.s,C.u,B.u,A.u";

    inline constexpr char kSequenceUvStart[] = "U.r,V.r,U.i,V.i";
    inline constexpr char kSequenceUvFull[]  = "U.r,V.r,U.i,V.i,V.s,U.s,V.u,U.u";

    inline constexpr char kSequenceSlotStart[] = "E.r,E.i";
    inline constexpr char kSequenceSlotFull[]  = "E.r,E.i,E.s,E.u";

    // 组 9：无槽位能力与混合 provision 的精确序列（N 不发槽、U 发槽 2）
    inline constexpr char kSequenceNoSlotStart[] = "N.r,N.i";
    inline constexpr char kSequenceNoSlotFull[]  = "N.r,N.i,N.s,N.u";
    inline constexpr char kSequenceMixedStart[]  = "N.r,U.r,N.i,U.i";
    inline constexpr char kSequenceMixedFull[]   = "N.r,U.r,N.i,U.i,U.s,N.s,U.u,N.u";

    // ---- 序列与断言辅助 ----

    void reset_sequence() noexcept;
    void append_sequence(const char* token) noexcept;
    const char* sequence_text() noexcept;
    bool sequence_equals(const char* expected) noexcept;

    void check_sequence(const char* case_name, const char* expected) noexcept;
    void check_code(const char* case_name, const PluginRuntimeDiagnostic& diagnostic, PluginRuntimeError expected) noexcept;
    void check_error(const char* case_name, PluginRuntimeError actual, PluginRuntimeError expected) noexcept;
    void check_state(const char* case_name, PluginRuntimeState expected) noexcept;
    void check_plugin_index(const char* case_name, const PluginRuntimeDiagnostic& diagnostic, uint16_t expected) noexcept;
    void check_slot_index(const char* case_name, const PluginRuntimeDiagnostic& diagnostic, int16_t expected) noexcept;
    void check_slots_empty(const char* case_name) noexcept;
    void check_service_address(const char* case_name, ServiceSlot slot, const void* expected) noexcept;
    void check_int32(const char* case_name, int32_t actual, int32_t expected) noexcept;
    void check_bool(const char* case_name, bool actual, bool expected) noexcept;

    /**
     *  @brief 同环境合法对照：kPlanAbcd 的完整生命周期（Ready -> Idle）
     *  @return 进入 Ready 且回到 Idle 时为真
     *  @note 只用于证明"失败用例失败不是因为环境坏了"；它自己不改任何模式开关，
     *        调用方必须保证 g_init_fail_mode 为 None、重入计划为空
     */
    bool legal_control_abcd() noexcept;

    // ---- 逐用例模式开关与回调内观测结果 ----

    void set_init_fail_mode(InitFailMode mode) noexcept;
    void set_register_mode(RegisterMode mode) noexcept;
    void set_cross_revoke(bool enabled) noexcept;
    void set_slot63_probe(bool enabled) noexcept;
    void set_reentry_plan(const PluginRuntimePlanView* plan) noexcept;

    PluginRuntimeError reentry_register_code() noexcept;
    PluginRuntimeError reentry_init_code() noexcept;
    PluginRuntimeError cross_revoke_code() noexcept;
    bool                cross_revoke_upstream_still_published() noexcept;
    bool                upstream_service_seen() noexcept;
    int32_t             upstream_add_result() noexcept;
    int32_t             upstream_square_result() noexcept;
    PluginRuntimeError  slot63_probe_code() noexcept;
    bool                slot63_empty_after_probe() noexcept;
    PluginRuntimeError  out_of_range_probe_code() noexcept;

    // ---- scratch 计划：逐用例只改一处时的可写静态副本 ----

    PluginRuntimePlanView&  scratch_plan() noexcept;
    PluginRuntimeRecord*    scratch_records() noexcept;
    uint16_t*               scratch_topo() noexcept;
    uint16_t*               scratch_owners() noexcept;
    PluginRuntimeProvision* scratch_provisions() noexcept;

    void reset_plan_abcd() noexcept;
    void reset_plan_uv() noexcept;
} // namespace ID::Bench::Runtime
