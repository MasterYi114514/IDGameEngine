#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "Common/Core.hpp"
#include "Common/templates/Singleton.hpp"
#include "Kernel/CoreTypes.hpp"
#include "Kernel/PluginCompileTime.hpp"
#include "Kernel/Plugin.hpp"

/**
 *  微内核运行期插件运行时（ID::PluginRuntime）
 *  1. 运行期唯一实例：只接收一份“已编译计划”的 POD 视图，按计划内给定的 canonical 拓扑序执行四阶段生命周期；
 *     不提供逐个登记描述符、动态追加插件、运行期依赖求解或拓扑重排（计划书 10.1）；
 *  2. 状态机（计划书 10.3）：Idle -> Registering -> Initializing -> Ready -> ShuttingDown -> Unloading -> Idle，
 *     失败路径走 RollingBack -> Idle。start 仅在 Idle 合法；Ready 重复 start 返回 AlreadyStarted；
 *     生命周期回调期间重入 start/shutdown 返回 Busy 且不改变当前流程；Idle 的 shutdown 幂等无操作；
 *  3. 失败清理（计划书 10.5）：on_init 一经调用就记入“已尝试”，即使返回 false 也要对它调用 on_shutdown；
 *     对已执行 on_register 的集合全部逆序 on_unload；随后强制清空槽位与计数回到 Idle，允许宿主再次 start；
 *  4. 服务槽（计划书 11.2）：写入全部走冷路径校验（槽位边界先于数组下标、阶段、owner、重复发布），
 *     读取 `get_service<T>()` 保持固定槽位下标 + 一次指针 load，不读状态、不加锁、不分配、不查字符串；
 *  5. 线程与所有权：`start` / `shutdown` / `publish_service` / `revoke_service` 只在宿主控制线程上调用，
 *     本类不加锁、不用原子做状态切换；Ready 期间业务线程只允许 `get_service<T>()`，
 *     宿主必须先停止外部业务访问再 shutdown（计划书 10.3 / 11.3）；
 *  6. 跨模块 ABI：`PluginRuntimePlanView` 只由固定宽度计数、原始指针与函数指针组成，带 abi_revision /
 *     struct_size 握手；不跨 DLL 传 STL 容器、tuple、异常、RTTI 或虚表。计划视图、记录、槽归属与服务表地址
 *     必须具有静态存储期并覆盖到 shutdown/unload 结束（计划书 10.2 / 11.3）；
 *  7. 本类是内核唯一的运行期插件入口：旧 `ID::PluginManager` 的运行期接口（单例、逐个描述符登记、
 *     初始化/关闭、运行期槽表与 Kahn 排序）已在 P4 退役，不存在与之并存的第二份状态或兼容包装。
 *
 *  @note 唯一实例的物理归属：STATIC 产品里由 ID 静态库拥有，SHARED 产品里由内核 DLL 拥有；
 *        不允许把含本类的静态 ID 分别链接进 EXE 与多个 DLL，否则服务槽与状态会各持一份（计划书 10.2）。
 */

namespace ID
{
    // 计划视图的 ABI 修订：布局或语义变化时必须递增，旧二进制一律重编（计划书 10.2）
    inline constexpr uint32_t kPluginRuntimeAbiRevision = 1;

    // 无下标/无槽位哨兵的唯一来源是 PluginCompileTime：运行期不再各自维护一份 0xFFFF / -1 字面量。
    // 这两个 using 只是把同一实体引到 ID 作用域，便于本头与实现按短名书写，不产生第二个常量。
    using PluginCompileTime::kNoIndex;
    using PluginCompileTime::kNoSlot;

    /**
     *  @brief 运行期状态机的稳定状态（计划书 10.3）
     *  @note 枚举值属于公共契约；`MaxStates` 是边界哨兵，不是合法状态。
     */
    enum class PluginRuntimeState : uint8_t
    {
        Idle = 0,           // 未启动：唯一允许 start 的状态
        Registering,        // 阶段 1（拓扑正序）：逐插件 on_register
        Initializing,       // 阶段 2（拓扑正序）：逐插件 on_init
        Ready,              // 全部插件就绪：槽表只读，业务线程可读取服务
        ShuttingDown,       // 阶段 3（拓扑逆序）：逐插件 on_shutdown
        Unloading,          // 阶段 4（拓扑逆序）：逐插件 on_unload
        RollingBack,        // 失败回滚：逆序 on_shutdown 已尝试 init 前缀，再逆序 on_unload 已注册集合
        MaxStates
    };

    /**
     *  @brief 运行期可恢复错误类别（计划书 10.6）
     *  @note 枚举值属于公共契约：增加类别或改名必须同步更新使用方（Benchmark 回归用例）。
     *        运行期不使用 Fatal/abort 表达这些错误，回滚路径必须可达；
     *        仅“内部不可恢复不变量被破坏”才保留断言。
     */
    enum class PluginRuntimeError : uint8_t
    {
        None = 0,                       // 无错误，调用成功

        // ---- 计划视图预检（Idle 内拒绝 start，不进入 Ready） ----
        PlanAbiMismatch,                // abi_revision 与 kPluginRuntimeAbiRevision 不一致
        PlanStructSizeMismatch,         // struct_size 与 sizeof(PluginRuntimePlanView) 不一致
        PlanTooManyPlugins,             // plugin_count 超过 kMaxPlugins(128)
        PlanNullPointer,                // 计划内必需数组指针为空
        PlanInvalidTopology,            // topo_order 不是 0..plugin_count-1 的排列（越界/重复/缺失）
        PlanInvalidRecord,              // 记录身份非法（空名/超长/末尾无 NUL/id 为 0/id 重复）
        PlanInvalidProvision,           // provides 记录非法（owner 越界/能力名非法/修订为 0/槽越界）
        PlanInvalidSlotOwner,           // slot_owner 越界，或与 provides 记录的 (owner, slot) 不一致

        // ---- 状态机 ----
        AlreadyStarted,                 // Ready 时重复 start：不重复执行任何回调
        Busy,                           // 生命周期回调期间重入 start/shutdown：不改变当前流程

        // ---- 生命周期 ----
        RegisterSlotMissing,            // on_register 返回后，本插件在计划中声明的槽未全部发布
        InitFailed,                     // on_init 返回 false（失败插件自身已记入“已尝试 init”）

        // ---- 服务槽冷路径写入 ----
        ServiceSlotOutOfRange,          // 槽位 >= 64（先于数组下标拒绝，含 MaxSlots 哨兵本身）
        ServicePublishRejected,         // 非 Registering 阶段发布（含 Ready 期间发布）
        ServiceRevokeRejected,          // 非 Unloading/RollingBack 阶段注销
        ServiceOwnerMismatch,           // 槽位不在编译期计划分配给当前插件的集合内
        ServiceDuplicatePublish,        // 同一槽位重复发布
        ServiceTableNull,               // 发布空表：注销必须走 revoke_service
        ServiceNotRevoked,              // on_unload 结束后槽位仍未被 owner 注销（记录后强制清空）

        MaxErrors
    };

    /**
     *  @brief 错误类别文本（诊断与回归输出用）
     *  @param code 错误类别
     *  @return 形如 "InitFailed" 的静态字符串
     */
    constexpr std::string_view plugin_runtime_error_name(PluginRuntimeError code) noexcept
    {
        switch (code)
        {
            case PluginRuntimeError::None:                     return "None";
            case PluginRuntimeError::PlanAbiMismatch:          return "PlanAbiMismatch";
            case PluginRuntimeError::PlanStructSizeMismatch:   return "PlanStructSizeMismatch";
            case PluginRuntimeError::PlanTooManyPlugins:       return "PlanTooManyPlugins";
            case PluginRuntimeError::PlanNullPointer:          return "PlanNullPointer";
            case PluginRuntimeError::PlanInvalidTopology:      return "PlanInvalidTopology";
            case PluginRuntimeError::PlanInvalidRecord:        return "PlanInvalidRecord";
            case PluginRuntimeError::PlanInvalidProvision:     return "PlanInvalidProvision";
            case PluginRuntimeError::PlanInvalidSlotOwner:     return "PlanInvalidSlotOwner";
            case PluginRuntimeError::AlreadyStarted:           return "AlreadyStarted";
            case PluginRuntimeError::Busy:                     return "Busy";
            case PluginRuntimeError::RegisterSlotMissing:      return "RegisterSlotMissing";
            case PluginRuntimeError::InitFailed:               return "InitFailed";
            case PluginRuntimeError::ServiceSlotOutOfRange:    return "ServiceSlotOutOfRange";
            case PluginRuntimeError::ServicePublishRejected:   return "ServicePublishRejected";
            case PluginRuntimeError::ServiceRevokeRejected:    return "ServiceRevokeRejected";
            case PluginRuntimeError::ServiceOwnerMismatch:     return "ServiceOwnerMismatch";
            case PluginRuntimeError::ServiceDuplicatePublish:  return "ServiceDuplicatePublish";
            case PluginRuntimeError::ServiceTableNull:         return "ServiceTableNull";
            case PluginRuntimeError::ServiceNotRevoked:        return "ServiceNotRevoked";
            case PluginRuntimeError::MaxErrors:                return "MaxErrors";
        }
        return "Unknown";
    }

    /**
     *  @brief 状态文本
     *  @param state 运行期状态
     *  @return 形如 "Registering" 的静态字符串
     */
    constexpr std::string_view plugin_runtime_state_name(PluginRuntimeState state) noexcept
    {
        switch (state)
        {
            case PluginRuntimeState::Idle:         return "Idle";
            case PluginRuntimeState::Registering:  return "Registering";
            case PluginRuntimeState::Initializing: return "Initializing";
            case PluginRuntimeState::Ready:        return "Ready";
            case PluginRuntimeState::ShuttingDown: return "ShuttingDown";
            case PluginRuntimeState::Unloading:    return "Unloading";
            case PluginRuntimeState::RollingBack:  return "RollingBack";
            case PluginRuntimeState::MaxStates:    return "MaxStates";
        }
        return "Unknown";
    }

    /**
     *  @brief 一个选中插件的四个生命周期回调地址（计划书 10.5 的固定形状）
     *  @note 四个函数指针都必须 `noexcept`；未提供某个阶段时写 nullptr，runtime 跳过该阶段。
     *        纯头文件插件（kind = interface + lifecycle = none）四项全为 nullptr 是合法记录：
     *        它即使被某个需求组命中，也只保留身份与编译期 usage，不创建回调槽、不占任何回调阶段。
     */
    struct PluginRuntimeCallbacks
    {
        void(*on_register)() noexcept = nullptr;    // 阶段 1：注册 TypeID、常量表与自己的服务槽
        bool(*on_init)() noexcept     = nullptr;    // 阶段 2：依赖已就绪，初始化子系统；返回 false 表示失败
        void(*on_shutdown)() noexcept = nullptr;    // 阶段 3：取消、排空并等待本插件任务，再释放运行期资源
        void(*on_unload)() noexcept   = nullptr;    // 阶段 4：注销自己写入的服务表
    };

    /**
     *  @brief 一条运行期插件记录（静态存储期，由组装层从编译期 AssemblyResult 的 canonical active 表派生）
     *  @note `name` 与 `capability` 都指向静态存储期的 NUL 结尾字符串，runtime 不复制也不释放；
     *        名字只用于身份核验与冷路径诊断，不参与任何热路径查找。
     *        `plugin_id` 必须等于编译期身份的 FNV-1a 64 值，预检会拒绝 0 与重复身份。
     */
    struct PluginRuntimeRecord
    {
        const char*             name      = nullptr;    // 精确插件名（<= 96 字节、NUL 结尾）
        uint64_t                plugin_id = 0;          // 编译期身份（PluginIdentity::id）
        PluginRuntimeCallbacks  callbacks = {};         // 四个生命周期回调地址
    };

    /**
     *  @brief 一条运行期能力记录（对应编译期 AssemblyResult 的 provision 扁平表一条）
     *  @note `slot` 为负数（PluginCompileTime::kNoSlot）表示纯头文件能力：没有运行期函数表，
     *        不参与“注册后必须已发布”的校验；其余能力与 slot_owner 逐项对应。
     */
    struct PluginRuntimeProvision
    {
        const char*  capability   = nullptr;                        // 能力名（静态存储期、NUL 结尾，仅冷路径使用）
        uint32_t     api_revision = 0;                              // 该实现的接口修订（>= 1，不是插件发布版本）
        uint16_t     plugin_index = kNoIndex;                       // 该能力在 records 中的 owner 下标
        int16_t      slot         = kNoSlot;                        // 0..63，或 kNoSlot 表示无运行期函数表
    };

    /**
     *  @brief 跨模块运行期计划视图（计划书 10.2 / 10.4 第 1 步的输入）
     *  @note 只由固定宽度计数、原始指针与函数指针组成，不携带 STL 容器/tuple/异常/RTTI/虚表，
     *        因此可以安全跨 DLL 传递；`abi_revision` 与 `struct_size` 是静态模块握手，
     *        不代表支持任意编译器混用或动态热重载。
     *  @note 视图本体与它指向的全部数组必须具有静态存储期，并覆盖到 shutdown/unload 结束；
     *        runtime 只保存指针，不复制内容（计划书 11.3）。
     *  @note 零插件产品合法：`plugin_count == 0` 时 records/topo_order/slot_owner/provisions 都可以为空，
     *        start 直接从 Idle 进入 Ready，shutdown 回到 Idle（计划书 12.3）。
     *  @note 字段来源逐项对应编译期 `AssemblyResult<Shape>`：records <- active_identity（名字与身份，
     *        四个回调地址由组装层的运行期绑定 TU 提供）、plugin_count <- active_count、
     *        topo_order <- topo_order、slot_owner <- slot_owner、provisions <- provision_owner /
     *        provision_slot / provision_api_revision / provision_capability、provision_count <- provision_count。
     *        视图不带逐需求 provider / matched 绑定：本产品的全部 matched 上游（同一需求组可以命中多个候选）
     *        已在编译期折进去重边与 topo_order，运行期只按 topo_order 执行四阶段生命周期，
     *        不做候选搜索、依赖重解或 fallback，也不为任何一条需求再查询上游；
     *        也不含任何指向常量求值局部对象的指针。
     */
    struct PluginRuntimePlanView
    {
        uint32_t abi_revision    = kPluginRuntimeAbiRevision;            // 必须等于 kPluginRuntimeAbiRevision
        uint32_t struct_size     = sizeof(PluginRuntimePlanView);        // 必须等于接收方 sizeof
        uint16_t plugin_count    = 0;                                    // 选中插件数（<= 128）
        uint16_t provision_count = 0;                                    // 选中插件的 provides 总数

        const PluginRuntimeRecord*    records    = nullptr;              // plugin_count 项
        const uint16_t*               topo_order = nullptr;              // plugin_count 项：canonical 拓扑序（值为 records 下标）
        const PluginRuntimeProvision* provisions = nullptr;              // provision_count 项
        const uint16_t*               slot_owner = nullptr;              // 64 项：槽位 -> records 下标，kNoIndex 为空槽
    };

    /**
     *  @brief 一次运行期调用的结构化诊断（计划书 10.6：带阶段、插件身份与槽位）
     *  @note `state` 是**出错时**所处的阶段；`start` 失败返回后 runtime 已经回到 Idle，
     *        因此不能把 `state` 当成调用结束后的状态。`code == None` 时 `state` 才是结束状态。
     */
    struct PluginRuntimeDiagnostic
    {
        PluginRuntimeError code      = PluginRuntimeError::None;          // 错误类别
        PluginRuntimeState state     = PluginRuntimeState::Idle;          // 出错阶段（成功时是结束状态）
        uint16_t           plugin    = kNoIndex;                          // 出错插件在 records 中的下标
        uint64_t           plugin_id = 0;                                 // 出错插件的编译期身份
        int16_t            slot      = kNoSlot;                           // 相关槽位；无槽位语义时为 kNoSlot

        /**
         *  @brief 本次调用是否成功
         *  @return code 为 None 时为真
         */
        ID_FORCEINLINE bool is_ok() const noexcept
        {
            return code == PluginRuntimeError::None;
        }
    };

    // 跨模块传递的结构必须是可平凡复制的 POD：不允许非平凡构造/析构、虚表或隐式所有权
    static_assert(std::is_trivially_copyable_v<PluginRuntimeCallbacks>, "PluginRuntimeCallbacks must be trivially copyable");
    static_assert(std::is_trivially_copyable_v<PluginRuntimeRecord>, "PluginRuntimeRecord must be trivially copyable");
    static_assert(std::is_trivially_copyable_v<PluginRuntimeProvision>, "PluginRuntimeProvision must be trivially copyable");
    static_assert(std::is_trivially_copyable_v<PluginRuntimePlanView>, "PluginRuntimePlanView must be trivially copyable");
    static_assert(std::is_trivially_copyable_v<PluginRuntimeDiagnostic>, "PluginRuntimeDiagnostic must be trivially copyable");

    /**
     *  运行期插件运行时：唯一实例，持有活动计划引用、固定槽位服务表与状态机。
     *  @note 槽位词汇复用 `ServiceSlot`（`Kernel/Plugin.hpp` 是 64 槽的唯一真源），
     *        避免运行期再维护第二套槽常量与实际枚举分叉。
     */
    class alignas(kCacheLineSize) ID_KERNEL_API PluginRuntime : public SingletonEager<PluginRuntime>
    {
        friend class SingletonEager<PluginRuntime>;

    public:
        static inline constexpr size_t kMaxPlugins = PluginCompileTime::kMaxActive;   // 与编译期选中上限同值
        static inline constexpr size_t kMaxSlots   = static_cast<size_t>(ServiceSlot::MaxSlots);

        /**
         *  @brief 安装静态计划并执行完整启动（阶段 1 register、阶段 2 init，成功后进入 Ready）
         *  @param plan 计划视图；视图本体与其数组必须在 shutdown/unload 结束前保持有效且只读
         *  @return 结构化诊断。成功时 code 为 None、state 为 Ready；失败时返回带阶段/插件/槽位的错误，
         *          且 runtime 已经回滚到 Idle，宿主可以修正条件后再次调用
         *  @note 只在宿主控制线程调用，只允许从 Idle 进入：Ready 重复调用返回 AlreadyStarted，
         *        回调期间重入返回 Busy，其它状态同样返回 Busy；这三种情况都不执行任何回调。
         *  @note 预检失败（ABI/尺寸/计数/身份/拓扑序/槽归属）返回 Plan* 错误且不改变状态。
         *        `register` 阶段的发布违约与 `init` 阶段的失败走 RollingBack：失败插件本身也会收到
         *        on_shutdown，已执行 on_register 的集合全部收到 on_unload，服务槽强制清空。
         */
        PluginRuntimeDiagnostic start(const PluginRuntimePlanView& plan) noexcept;

        /**
         *  @brief 逆序执行 on_shutdown 与 on_unload，验证并强制清空槽位后回到 Idle
         *  @return 结构化诊断。成功时 code 为 None、state 为 Idle；回调期间重入返回 Busy 且不改变流程
         *  @note Idle 调用是幂等无操作；非 Idle 且非 Ready 的任何状态都返回 Busy。
         *  @note 宿主必须在调用前停止外部业务访问：本函数不提供读者代际、epoch 或并发撤销协议，
         *        插件自身任务由各插件 on_shutdown 收敛（计划书 11.3）。
         *  @note on_unload 之后仍有 owner 未注销的槽会被记录为 ServiceNotRevoked 并强制清空，
         *        因此关闭结束后 64 个槽必然全部为空。
         */
        PluginRuntimeDiagnostic shutdown() noexcept;

        /**
         *  @brief 冷路径：把自己的服务表发布到计划分配给本插件的槽位
         *  @param slot 槽位索引，合法范围 [0, 64)。边界在数组下标之前检查
         *  @param api_table 函数表地址，必须具有静态存储期并覆盖到本插件 on_unload 结束；不能为空
         *  @return 结构化诊断。成功时 code 为 None；失败返回具体违约原因，槽表内容不变
         *  @note 只在插件自己的 on_register 内调用：非 Registering 阶段（含 Ready 期间）一律拒绝；
         *        只能写编译期计划分配给当前插件（slot_owner[slot] == 当前插件）的槽，
         *        不覆盖别人的槽，不重复发布同一槽（计划书 11.2）。
         *  @note 返回值即使被调用方忽略，本条违约也会被 runtime 记入本次生命周期：
         *        on_register 返回后 start 会回滚并返回该错误，不会悄悄进入 Ready。
         */
        PluginRuntimeDiagnostic publish_service(uint32_t slot, const void* api_table) noexcept;

        /**
         *  @brief 冷路径：注销自己的服务表
         *  @param slot 槽位索引，合法范围 [0, 64)。边界在数组下标之前检查
         *  @return 结构化诊断。成功（含“本来就没发布”）时 code 为 None
         *  @note 只在插件自己的 on_unload（或失败回滚的 unload 阶段）内调用：
         *        非 Unloading/RollingBack 阶段一律拒绝；只能清除 slot_owner 指向当前插件的槽，
         *        不能清除别人的服务。
         */
        PluginRuntimeDiagnostic revoke_service(uint32_t slot) noexcept;

        /**
         *  @brief 按固定槽位取出函数表（热路径）
         *  @param slot 槽位枚举值，必须小于 ServiceSlot::MaxSlots
         *  @return 槽位上的函数表指针；槽位为空时返回 nullptr
         *  @note 成本与 P4 退役前的运行期槽表相同：一次固定下标 + 一次指针 load（迁移没有引入
         *        额外间接层）；
         *        不读状态、不加锁、不分配、不做字符串查找、不做所有权或版本校验（计划书 11.2）。
         *        所有权与生命周期由写入方在冷路径保证；`ApiTableType` 必须与发布时写入的表类型一致，
         *        读取时序由调用方保证在 Ready 区间内。
         */
        template <typename ApiTableType>
        ID_FORCEINLINE const ApiTableType* get_service(ServiceSlot slot) const noexcept
        {
            return static_cast<const ApiTableType*>(m_service_slots[static_cast<size_t>(slot)]);
        }

        /**
         *  @brief 当前状态机的状态（只用于观测与冷路径判定）
         *  @return 状态枚举值
         */
        ID_FORCEINLINE PluginRuntimeState state() const noexcept
        {
            return m_state;
        }

    protected:
        constexpr PluginRuntime() noexcept  = default;
        ~PluginRuntime() noexcept           = default;

    private:
        /**
         *  @brief 冷路径：预检计划视图的 ABI、尺寸、计数、拓扑序、记录身份与槽位归属
         *  @param plan 计划视图
         *  @return 第一条违约的结构化诊断；全部通过时 code 为 None
         *  @note 只做结构自洽检查，不重新求解静态依赖图、不重算哈希（计划书 10.6）。
         */
        ID_NOINLINE PluginRuntimeDiagnostic validate_plan(const PluginRuntimePlanView& plan) const noexcept;

        /**
         *  @brief 冷路径：某插件 on_register 返回后，检查它在计划里声明的槽是否都已发布
         *  @param plan 活动计划
         *  @param index 当前插件的 records 下标
         *  @return 第一条缺失发布的结构化诊断；全部已发布时 code 为 None
         *  @note 这是计划书 10.4 第 3 步的增量形式：违约时立即停止后续注册，而不是等全部注册完再发现。
         */
        ID_NOINLINE PluginRuntimeDiagnostic check_slot_publication(const PluginRuntimePlanView& plan, uint16_t index) const noexcept;

        /**
         *  @brief 冷路径：记录本次生命周期内的第一条槽位违约并把它返回给调用方
         *  @param diagnostic 待记录的诊断
         *  @return 原样返回 diagnostic
         *  @note 只在写入冷路径（publish/revoke 违约）与回滚期调用，因此整条函数体从调用点剥离：
         *        `ID_COLD` 在 MSVC 下为空，必须与 `ID_NOINLINE` 配套使用（cpp-style 第 7 节）
         */
        ID_NOINLINE ID_COLD PluginRuntimeDiagnostic record_slot_violation(PluginRuntimeDiagnostic diagnostic) noexcept;

        /**
         *  @brief 冷路径：失败回滚（逆序 on_shutdown 已尝试 init 前缀、逆序 on_unload 已注册集合、清空槽位）
         *  @note 结束时状态为 Idle、活动计划引用被释放、计数归零，因此可以再次 start。
         */
        ID_NOINLINE ID_COLD void rollback() noexcept;

        /**
         *  @brief 冷路径：当前回调所属插件的身份
         *  @return 当前 owner 的 plugin_id；没有活动 owner 时返回 0
         */
        uint64_t current_plugin_id() const noexcept;

    private:
        static PluginRuntime s_instance;                                             // 唯一实例（.cpp 里 constinit 定义）

        const void*                   m_service_slots[kMaxSlots] = { nullptr };      // 热路径：固定槽位服务指针表
        const PluginRuntimePlanView*  m_active_plan              = nullptr;          // 活动计划（仅生命周期期有效）
        PluginRuntimeState            m_state                    = PluginRuntimeState::Idle;
        uint16_t                      m_current_owner            = kNoIndex;         // 当前回调所属插件下标
        uint16_t                      m_registered_count         = 0;                // 已调用 on_register 的插件数
        uint16_t                      m_init_count               = 0;                // 已调用 on_init 的插件数（含失败插件）
        PluginRuntimeDiagnostic       m_slot_violation           = {};               // 本次生命周期内第一条槽位违约
    };
} // namespace ID
