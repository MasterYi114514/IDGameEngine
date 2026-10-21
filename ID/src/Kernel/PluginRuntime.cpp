#include <cstdio>
#include <cstring>
#include <string_view>

#include "Kernel/PluginRuntime.hpp"
#include "Kernel/BootstrapLog.hpp"

namespace ID
{
    namespace
    {
        /**
         *  @brief 组装一条结构化诊断
         *  @param code 错误类别
         *  @param state 出错时所处的状态
         *  @param plugin 相关插件在 records 中的下标
         *  @param plugin_id 相关插件的编译期身份
         *  @param slot 相关槽位；无槽位语义时用 kNoSlot
         *  @return 填好的诊断
         */
        ID_FORCEINLINE PluginRuntimeDiagnostic make_diagnostic(PluginRuntimeError code, PluginRuntimeState state,
                                                               uint16_t plugin = PluginCompileTime::kNoIndex, uint64_t plugin_id = 0,
                                                               int16_t slot = PluginCompileTime::kNoSlot) noexcept
        {
            PluginRuntimeDiagnostic diagnostic{};
            diagnostic.code      = code;
            diagnostic.state     = state;
            diagnostic.plugin    = plugin;
            diagnostic.plugin_id = plugin_id;
            diagnostic.slot      = slot;
            return diagnostic;
        }

        /**
         *  @brief 冷路径：把无法通过返回值上报的诊断写进自举日志
         *  @param plan 活动计划，用于取插件名；可为空
         *  @param diagnostic 待输出的诊断
         *  @note 只在关闭/回滚期的违约分支使用：格式化 + IO 属于冷路径，绝不出现在 get_service 侧。
         */
        ID_NOINLINE ID_COLD void report_diagnostic(const PluginRuntimePlanView* plan, const PluginRuntimeDiagnostic& diagnostic) noexcept
        {
            const std::string_view code_text  = plugin_runtime_error_name(diagnostic.code);
            const std::string_view state_text = plugin_runtime_state_name(diagnostic.state);

            std::string_view name_text = {};
            if (plan != nullptr && plan->records != nullptr && diagnostic.plugin < plan->plugin_count)
            {
                const char* const name = plan->records[diagnostic.plugin].name;
                name_text = (name != nullptr) ? std::string_view{ name } : std::string_view{};
            }

            char message[320];
            std::snprintf(message, sizeof(message), "PluginRuntime %.*s (state=%.*s, plugin='%.*s', id=%llu, slot=%d)",
                static_cast<int>(code_text.size()), code_text.data(),
                static_cast<int>(state_text.size()), state_text.data(),
                static_cast<int>(name_text.size()), name_text.data(),
                static_cast<unsigned long long>(diagnostic.plugin_id),
                static_cast<int>(diagnostic.slot));
            ID_KERNEL_LOG(Log::Level::Error, message);
        }
    } // namespace

    // 确保全工程（含 DLL 边界）只有一份运行期实例，且没有运行期构造函数
    constinit PluginRuntime PluginRuntime::s_instance;

    uint64_t PluginRuntime::current_plugin_id() const noexcept
    {
        if (m_active_plan == nullptr || m_active_plan->records == nullptr || m_current_owner >= m_active_plan->plugin_count)
        {
            return 0;
        }
        return m_active_plan->records[m_current_owner].plugin_id;
    }

    ID_NOINLINE ID_COLD PluginRuntimeDiagnostic PluginRuntime::record_slot_violation(PluginRuntimeDiagnostic diagnostic) noexcept
    {
        // 只保留第一条：后续违约通常是第一条的连锁反应，报第一条才指向根因
        if (m_slot_violation.code == PluginRuntimeError::None)
        {
            m_slot_violation = diagnostic;
        }
        return diagnostic;
    }

    PluginRuntimeDiagnostic PluginRuntime::validate_plan(const PluginRuntimePlanView& plan) const noexcept
    {
        // 1. 跨模块握手：修订与结构尺寸先对上，之后才允许假设字段语义
        if (plan.abi_revision != kPluginRuntimeAbiRevision)
        {
            return make_diagnostic(PluginRuntimeError::PlanAbiMismatch, PluginRuntimeState::Idle);
        }
        if (plan.struct_size != sizeof(PluginRuntimePlanView))
        {
            return make_diagnostic(PluginRuntimeError::PlanStructSizeMismatch, PluginRuntimeState::Idle);
        }
        if (plan.plugin_count > kMaxPlugins)
        {
            return make_diagnostic(PluginRuntimeError::PlanTooManyPlugins, PluginRuntimeState::Idle);
        }

        // 零插件是合法的纯内核产品：没有任何记录可校验，但也不允许夹带 provides（计划书 12.3）
        if (plan.plugin_count == 0)
        {
            if (plan.provision_count != 0)
            {
                return make_diagnostic(PluginRuntimeError::PlanInvalidProvision, PluginRuntimeState::Idle);
            }
            return PluginRuntimeDiagnostic{};
        }

        if (plan.records == nullptr || plan.topo_order == nullptr || plan.slot_owner == nullptr)
        {
            return make_diagnostic(PluginRuntimeError::PlanNullPointer, PluginRuntimeState::Idle);
        }
        if (plan.provision_count > 0 && plan.provisions == nullptr)
        {
            return make_diagnostic(PluginRuntimeError::PlanNullPointer, PluginRuntimeState::Idle);
        }

        // 2. 拓扑序必须是 0..plugin_count-1 的排列：漏项会让插件少跑一个阶段，重复会让回调跑两次
        uint64_t seen[2] = { 0, 0 };
        for (uint16_t i = 0; i < plan.plugin_count; ++i)
        {
            const uint16_t index = plan.topo_order[i];
            if (index >= plan.plugin_count)
            {
                return make_diagnostic(PluginRuntimeError::PlanInvalidTopology, PluginRuntimeState::Idle, index);
            }
            const uint64_t bit = 1ull << (index & 63u);
            uint64_t&      word = seen[index >> 6];
            if ((word & bit) != 0)
            {
                return make_diagnostic(PluginRuntimeError::PlanInvalidTopology, PluginRuntimeState::Idle, index);
            }
            word |= bit;
        }

        // 3. 记录身份：名字合法且非空、身份非零且唯一。哈希重算属于编译期职责，运行期只做结构自洽
        for (uint16_t i = 0; i < plan.plugin_count; ++i)
        {
            const PluginRuntimeRecord& record = plan.records[i];
            if (record.name == nullptr || record.name[0] == '\0' || std::strlen(record.name) > PluginCompileTime::kMaxNameBytes)
            {
                return make_diagnostic(PluginRuntimeError::PlanInvalidRecord, PluginRuntimeState::Idle, i);
            }
            if (record.plugin_id == 0)
            {
                return make_diagnostic(PluginRuntimeError::PlanInvalidRecord, PluginRuntimeState::Idle, i);
            }
            for (uint16_t j = 0; j < i; ++j)
            {
                if (plan.records[j].plugin_id == record.plugin_id)
                {
                    return make_diagnostic(PluginRuntimeError::PlanInvalidRecord, PluginRuntimeState::Idle, i, record.plugin_id);
                }
            }
        }

        // 4. provides 记录：owner、能力名、接口修订与槽位都必须落在自洽范围内
        uint64_t declared_slots = 0;    // 被 provides 声明占用的槽位位图
        for (uint16_t p = 0; p < plan.provision_count; ++p)
        {
            const PluginRuntimeProvision& provision = plan.provisions[p];
            const int16_t                 slot      = provision.slot;

            if (provision.plugin_index >= plan.plugin_count)
            {
                return make_diagnostic(PluginRuntimeError::PlanInvalidProvision, PluginRuntimeState::Idle,
                    provision.plugin_index, 0, slot);
            }
            if (provision.capability == nullptr || provision.capability[0] == '\0'
                || std::strlen(provision.capability) > PluginCompileTime::kMaxNameBytes
                || provision.api_revision == 0)
            {
                return make_diagnostic(PluginRuntimeError::PlanInvalidProvision, PluginRuntimeState::Idle,
                    provision.plugin_index, 0, slot);
            }
            // 先接受无槽位哨兵：interface/纯头插件的 provides 不发布函数表（与编译期 kNoSlot 语义一致）。
            // 若把它当越界先拒绝，任何“能力无槽位”的合法计划都会在 start 前被判非法。
            if (slot == PluginCompileTime::kNoSlot)
            {
                continue;
            }
            // 其余值全部按有符号范围判：kNoSlot 已在上一步处理，这里再出现负数同样非法。
            // 不能用 size_t(slot) 比较——那会把 -1 变成一个巨大的无符号值，正是上面那条缺陷的成因
            if (slot < 0 || slot >= static_cast<int16_t>(kMaxSlots))
            {
                return make_diagnostic(PluginRuntimeError::PlanInvalidProvision, PluginRuntimeState::Idle,
                    provision.plugin_index, 0, slot);
            }

            const size_t slot_index = static_cast<size_t>(slot);
            if ((declared_slots & (1ull << slot_index)) != 0)
            {
                // 两条 provides 抢同一个槽：产品级槽位唯一，冲突必须在计划里就被拒绝
                return make_diagnostic(PluginRuntimeError::PlanInvalidSlotOwner, PluginRuntimeState::Idle,
                    provision.plugin_index, 0, slot);
            }
            declared_slots |= 1ull << slot_index;
            if (plan.slot_owner[slot_index] != provision.plugin_index)
            {
                return make_diagnostic(PluginRuntimeError::PlanInvalidSlotOwner, PluginRuntimeState::Idle,
                    provision.plugin_index, 0, slot);
            }
        }

        // 5. 槽归属表：要么空，要么指向真实 owner 且必须对应一条 provides，
        //    否则“注册后必须已发布”与“只能写自己的槽”都无从校验
        for (size_t slot = 0; slot < kMaxSlots; ++slot)
        {
            const uint16_t owner = plan.slot_owner[slot];
            if (owner == PluginCompileTime::kNoIndex)
            {
                continue;
            }
            if (owner >= plan.plugin_count || (declared_slots & (1ull << slot)) == 0)
            {
                return make_diagnostic(PluginRuntimeError::PlanInvalidSlotOwner, PluginRuntimeState::Idle,
                    owner, 0, static_cast<int16_t>(slot));
            }
        }
        return PluginRuntimeDiagnostic{};
    }

    PluginRuntimeDiagnostic PluginRuntime::check_slot_publication(const PluginRuntimePlanView& plan, uint16_t index) const noexcept
    {
        for (uint16_t p = 0; p < plan.provision_count; ++p)
        {
            const PluginRuntimeProvision& provision = plan.provisions[p];
            if (provision.plugin_index != index || provision.slot < 0)
            {
                continue;
            }
            if (m_service_slots[static_cast<size_t>(provision.slot)] == nullptr)
            {
                return make_diagnostic(PluginRuntimeError::RegisterSlotMissing, m_state, index,
                    plan.records[index].plugin_id, provision.slot);
            }
        }
        return PluginRuntimeDiagnostic{};
    }

    PluginRuntimeDiagnostic PluginRuntime::start(const PluginRuntimePlanView& plan) noexcept
    {
        // 状态机入口：Ready 重复启动、回调期间重入都在这里拒绝，且不执行任何回调
        switch (m_state)
        {
            case PluginRuntimeState::Idle:  break;
            case PluginRuntimeState::Ready: return make_diagnostic(PluginRuntimeError::AlreadyStarted, m_state);
            default:                        return make_diagnostic(PluginRuntimeError::Busy, m_state);
        }

        // 1. 预检：ABI / 结构尺寸 / 计数 / 记录身份 / 拓扑序 / 槽位归属。违约直接返回，状态仍是 Idle
        PluginRuntimeDiagnostic diagnostic = validate_plan(plan);
        if (!diagnostic.is_ok())
        {
            return diagnostic;
        }

        m_active_plan      = &plan;
        m_registered_count = 0;
        m_init_count       = 0;
        m_current_owner    = PluginCompileTime::kNoIndex;
        m_slot_violation   = PluginRuntimeDiagnostic{};

        // 2. 阶段 1（拓扑正序）：全部选中插件的 on_register
        m_state = PluginRuntimeState::Registering;
        for (uint16_t i = 0; i < plan.plugin_count; ++i)
        {
            const uint16_t             index  = plan.topo_order[i];
            const PluginRuntimeRecord& record = plan.records[index];
            m_current_owner = index;

            // 调用前就记为“已尝试 register”：发布违约时对已执行集合逆序 unload 才有完整覆盖
            // （与 init 阶段“调用前记入”的口径一致，计划书 10.5）
            ++m_registered_count;

            if (record.callbacks.on_register != nullptr)
            {
                record.callbacks.on_register();
            }

            // 该插件声明的槽必须都已发布（计划书 10.4 第 3 步的增量形式），违约立即停止后续注册
            diagnostic = check_slot_publication(plan, index);
            if (diagnostic.is_ok() && m_slot_violation.code != PluginRuntimeError::None)
            {
                // 声明槽已齐但发布过程有违约（例如先企图占别人的槽）：调用方忽略了返回值也要在这里停下。
                // 槽位缺失优先于它上报，因为那直接说明本插件没有兑现计划里的声明
                diagnostic = m_slot_violation;
            }
            if (!diagnostic.is_ok())
            {
                rollback();
                return diagnostic;
            }
        }

        // 3. 阶段 2（拓扑正序）：on_init。调用前就记入“已尝试 init”，失败插件自身也必须收到 on_shutdown
        m_state = PluginRuntimeState::Initializing;
        for (uint16_t i = 0; i < plan.plugin_count; ++i)
        {
            const uint16_t             index  = plan.topo_order[i];
            const PluginRuntimeRecord& record = plan.records[index];
            m_current_owner = index;
            ++m_init_count;

            if (record.callbacks.on_init != nullptr && !record.callbacks.on_init())
            {
                diagnostic = make_diagnostic(PluginRuntimeError::InitFailed, m_state, index, record.plugin_id);
                rollback();
                return diagnostic;
            }
        }

        // 4. 成功：进入 Ready。此后槽表只读，业务线程可以开始读取服务
        m_current_owner = PluginCompileTime::kNoIndex;
        m_state = PluginRuntimeState::Ready;
        return make_diagnostic(PluginRuntimeError::None, PluginRuntimeState::Ready);
    }

    void PluginRuntime::rollback() noexcept
    {
        ID_KERNEL_ASSERT(m_active_plan != nullptr, "PluginRuntime::rollback() requires an installed plan!");

        const PluginRuntimePlanView& plan = *m_active_plan;
        m_state = PluginRuntimeState::RollingBack;

        // 1. 已尝试 init 的前缀逆序 shutdown：包含返回 false 的那个插件，它必须清理自己的部分初始化。
        //    逆拓扑保证任务收敛期间它所依赖的上游仍然存活
        for (uint16_t i = m_init_count; i > 0; --i)
        {
            const uint16_t             index  = plan.topo_order[i - 1];
            const PluginRuntimeRecord& record = plan.records[index];
            m_current_owner = index;
            if (record.callbacks.on_shutdown != nullptr)
            {
                record.callbacks.on_shutdown();
            }
        }

        // 2. 已执行 register 的集合全部逆序 unload：注册了但没 init 的插件同样必须注销，
        //    旧实现只回滚已 init 前缀，这部分会泄漏
        for (uint16_t i = m_registered_count; i > 0; --i)
        {
            const uint16_t             index  = plan.topo_order[i - 1];
            const PluginRuntimeRecord& record = plan.records[index];
            m_current_owner = index;
            if (record.callbacks.on_unload != nullptr)
            {
                record.callbacks.on_unload();
            }
        }

        // 3. 强制清空槽位与计数、释放计划引用，回到 Idle：宿主修正条件后可以再次 start
        for (size_t slot = 0; slot < kMaxSlots; ++slot)
        {
            m_service_slots[slot] = nullptr;
        }
        m_slot_violation   = PluginRuntimeDiagnostic{};
        m_current_owner    = PluginCompileTime::kNoIndex;
        m_registered_count = 0;
        m_init_count       = 0;
        m_active_plan      = nullptr;
        m_state            = PluginRuntimeState::Idle;
    }

    PluginRuntimeDiagnostic PluginRuntime::shutdown() noexcept
    {
        // Idle 的 shutdown 幂等无操作
        if (m_state == PluginRuntimeState::Idle)
        {
            return make_diagnostic(PluginRuntimeError::None, PluginRuntimeState::Idle);
        }

        // 回调期间重入不改变当前流程
        if (m_state != PluginRuntimeState::Ready)
        {
            return make_diagnostic(PluginRuntimeError::Busy, m_state);
        }

        // Ready 必然由一次成功的 start 建立，活动计划一定在；破坏它属于内部不变量错误。
        // 只断言真正必需的那一条：零插件产品按头文件契约允许 records/topo_order/slot_owner/provisions
        // 全为空（计划书 12.3），把“表非空”也写进断言会让合法的空产品在关闭时 abort
        ID_KERNEL_ASSERT(m_active_plan != nullptr,
                         "PluginRuntime::shutdown() reached Ready without an installed plan!");

        const PluginRuntimePlanView& plan = *m_active_plan;

        // 阶段 3（拓扑逆序）：on_shutdown 先停任务再放资源，下游先于上游
        m_state = PluginRuntimeState::ShuttingDown;
        for (uint16_t i = plan.plugin_count; i > 0; --i)
        {
            const uint16_t             index  = plan.topo_order[i - 1];
            const PluginRuntimeRecord& record = plan.records[index];
            m_current_owner = index;
            if (record.callbacks.on_shutdown != nullptr)
            {
                record.callbacks.on_shutdown();
            }
        }

        // 阶段 4（拓扑逆序）：on_unload 注销自己写入的服务表
        m_state = PluginRuntimeState::Unloading;
        for (uint16_t i = plan.plugin_count; i > 0; --i)
        {
            const uint16_t             index  = plan.topo_order[i - 1];
            const PluginRuntimeRecord& record = plan.records[index];
            m_current_owner = index;
            if (record.callbacks.on_unload != nullptr)
            {
                record.callbacks.on_unload();
            }
        }

        // 注销阶段本身被拒的调用（例如越界槽位）无法通过返回值上报，只能在这里记录
        if (m_slot_violation.code != PluginRuntimeError::None)
        {
            report_diagnostic(&plan, m_slot_violation);
        }

        // 阶段 5：验证并强制清空运行期槽位。on_unload 没有注销的槽在这里留证后强制清空
        for (size_t slot = 0; slot < kMaxSlots; ++slot)
        {
            if (m_service_slots[slot] == nullptr)
            {
                continue;
            }
            if (plan.slot_owner != nullptr && plan.slot_owner[slot] != PluginCompileTime::kNoIndex)
            {
                const uint16_t owner = plan.slot_owner[slot];
                report_diagnostic(&plan, make_diagnostic(PluginRuntimeError::ServiceNotRevoked, PluginRuntimeState::Unloading,
                    owner, plan.records[owner].plugin_id, static_cast<int16_t>(slot)));
            }
            m_service_slots[slot] = nullptr;
        }

        m_slot_violation   = PluginRuntimeDiagnostic{};
        m_current_owner    = PluginCompileTime::kNoIndex;
        m_registered_count = 0;
        m_init_count       = 0;
        m_active_plan      = nullptr;
        m_state            = PluginRuntimeState::Idle;
        return make_diagnostic(PluginRuntimeError::None, PluginRuntimeState::Idle);
    }

    PluginRuntimeDiagnostic PluginRuntime::publish_service(uint32_t slot, const void* api_table) noexcept
    {
        // 越界槽位在诊断里用 int16 上限表示：值本身远超槽位范围，不做有损截断冒充合法下标
        const int16_t reported_slot = (slot <= 0x7FFFu) ? static_cast<int16_t>(slot) : static_cast<int16_t>(0x7FFF);

        // 1. 边界先于数组下标：MaxSlots(64) 本身不是合法槽位
        if (slot >= kMaxSlots)
        {
            return record_slot_violation(make_diagnostic(PluginRuntimeError::ServiceSlotOutOfRange, m_state,
                m_current_owner, current_plugin_id(), reported_slot));
        }

        // 2. 只有 Registering 阶段可以发布：Ready 期间槽表只读，初始化/关闭/回滚阶段一律拒绝
        if (m_state != PluginRuntimeState::Registering)
        {
            return record_slot_violation(make_diagnostic(PluginRuntimeError::ServicePublishRejected, m_state,
                m_current_owner, current_plugin_id(), reported_slot));
        }

        ID_KERNEL_ASSERT(m_active_plan != nullptr && m_active_plan->slot_owner != nullptr,
                         "PluginRuntime::publish_service() reached without an installed plan!");

        // 3. 空表不是发布；注销必须走 revoke_service
        if (api_table == nullptr)
        {
            return record_slot_violation(make_diagnostic(PluginRuntimeError::ServiceTableNull, m_state,
                m_current_owner, current_plugin_id(), reported_slot));
        }

        // 4. 所有权：只能写编译期计划分配给当前插件的槽，不覆盖他人
        if (m_active_plan->slot_owner[slot] != m_current_owner)
        {
            return record_slot_violation(make_diagnostic(PluginRuntimeError::ServiceOwnerMismatch, m_state,
                m_current_owner, current_plugin_id(), reported_slot));
        }

        // 5. 同一槽不重复发布
        if (m_service_slots[slot] != nullptr)
        {
            return record_slot_violation(make_diagnostic(PluginRuntimeError::ServiceDuplicatePublish, m_state,
                m_current_owner, current_plugin_id(), reported_slot));
        }

        m_service_slots[slot] = api_table;
        return make_diagnostic(PluginRuntimeError::None, m_state);
    }

    PluginRuntimeDiagnostic PluginRuntime::revoke_service(uint32_t slot) noexcept
    {
        const int16_t reported_slot = (slot <= 0x7FFFu) ? static_cast<int16_t>(slot) : static_cast<int16_t>(0x7FFF);

        // 1. 边界先于数组下标
        if (slot >= kMaxSlots)
        {
            return record_slot_violation(make_diagnostic(PluginRuntimeError::ServiceSlotOutOfRange, m_state,
                m_current_owner, current_plugin_id(), reported_slot));
        }

        // 2. 只有 Unloading 与失败回滚允许注销
        if (m_state != PluginRuntimeState::Unloading && m_state != PluginRuntimeState::RollingBack)
        {
            return record_slot_violation(make_diagnostic(PluginRuntimeError::ServiceRevokeRejected, m_state,
                m_current_owner, current_plugin_id(), reported_slot));
        }

        ID_KERNEL_ASSERT(m_active_plan != nullptr && m_active_plan->slot_owner != nullptr,
                         "PluginRuntime::revoke_service() reached without an installed plan!");

        // 3. on_unload 只能清除本插件自己的槽，不能清除别人的服务
        if (m_active_plan->slot_owner[slot] != m_current_owner)
        {
            return record_slot_violation(make_diagnostic(PluginRuntimeError::ServiceOwnerMismatch, m_state,
                m_current_owner, current_plugin_id(), reported_slot));
        }

        // 本来就没发布过也视为成功：部分初始化失败的插件必须能安全走完注销
        m_service_slots[slot] = nullptr;
        return make_diagnostic(PluginRuntimeError::None, m_state);
    }
} // namespace ID
