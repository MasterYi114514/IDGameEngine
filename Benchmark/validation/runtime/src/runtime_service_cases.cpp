#include <cstdio>

#include "RuntimeCases.hpp"
#include "RuntimeFixture.hpp"

/**
 *  组 4/6/7：register 阶段发布违约、槽位边界与所有权、真实调用已选上游的公开函数
 *  1. 四种发布违约（缺槽 / 错 owner / 重复发布 / 越界 64）都必须在 start 内停止后续注册、
 *     对已执行集合逆序 unload、不进入 init，并留下结构化错误码；
 *  2. 每个违约用例紧跟同一计划的合法对照，证明失败不是环境问题；
 *  3. 组 7 通过 get_service 取表后真正调用函数并核对返回值，不是只判非空。
 */

namespace ID::Bench::Runtime
{
    namespace
    {
        // 组 4 的四条违约：各自期望的错误码与诊断里应报告的槽位
        struct RegisterViolationCase
        {
            RegisterMode       mode;
            const char*        leaf;
            PluginRuntimeError expected;
            int16_t            slot;
        };

        constexpr RegisterViolationCase kRegisterViolations[4] = {
            { RegisterMode::Missing,    "missing_slot",       PluginRuntimeError::RegisterSlotMissing,     2  },
            { RegisterMode::WrongOwner, "wrong_owner",        PluginRuntimeError::ServiceOwnerMismatch,    3  },
            { RegisterMode::Duplicate,  "duplicate_publish",  PluginRuntimeError::ServiceDuplicatePublish, 2  },
            { RegisterMode::OutOfRange, "slot_out_of_range",  PluginRuntimeError::ServiceSlotOutOfRange,   64 }
        };
    } // namespace

    void run_service_cases() noexcept
    {
        PluginRuntime& runtime = PluginRuntime::instance();
        char name[96];

        // ---------- 组 4：register 阶段发布违约 ----------
        // 同环境合法对照：同一计划、同一夹具，U/V 都按计划发布
        set_register_mode(RegisterMode::Ok);
        reset_sequence();
        PluginRuntimeDiagnostic diagnostic = runtime.start(kPlanUv);
        check_code("register.control_start_ok", diagnostic, PluginRuntimeError::None);
        check_state("register.control_ready", PluginRuntimeState::Ready);
        check_sequence("register.control_order", kSequenceUvStart);
        check_service_address("register.control_slot2_table", ServiceSlot::Math, &kMathTable);
        check_service_address("register.control_slot3_table", static_cast<ServiceSlot>(3), &kAuxTable);
        diagnostic = runtime.shutdown();
        check_code("register.control_shutdown_ok", diagnostic, PluginRuntimeError::None);
        check_sequence("register.control_full_order", kSequenceUvFull);
        check_slots_empty("register.control_slots_empty");

        for (size_t i = 0; i < 4; ++i)
        {
            const RegisterViolationCase& violation = kRegisterViolations[i];

            set_register_mode(violation.mode);
            reset_sequence();
            diagnostic = runtime.start(kPlanUv);

            std::snprintf(name, sizeof(name), "register.%s.code", violation.leaf);
            check_code(name, diagnostic, violation.expected);
            std::snprintf(name, sizeof(name), "register.%s.plugin", violation.leaf);
            check_plugin_index(name, diagnostic, 0);
            std::snprintf(name, sizeof(name), "register.%s.slot", violation.leaf);
            check_slot_index(name, diagnostic, violation.slot);
            std::snprintf(name, sizeof(name), "register.%s.idle", violation.leaf);
            check_state(name, PluginRuntimeState::Idle);
            // 停止后续注册（V 没有 on_register）、不进入 init、已执行集合逆序 unload（只有 U）
            std::snprintf(name, sizeof(name), "register.%s.rollback_order", violation.leaf);
            check_sequence(name, "U.r,U.u");
            std::snprintf(name, sizeof(name), "register.%s.slots_empty", violation.leaf);
            check_slots_empty(name);

            if (violation.mode == RegisterMode::OutOfRange)
            {
                check_error("register.out_of_range_probe_code", out_of_range_probe_code(),
                            PluginRuntimeError::ServiceSlotOutOfRange);
            }

            // 同环境合法对照：同一计划回到合法模式必须进入 Ready
            set_register_mode(RegisterMode::Ok);
            reset_sequence();
            diagnostic = runtime.start(kPlanUv);
            std::snprintf(name, sizeof(name), "register.%s.legal_control", violation.leaf);
            check_code(name, diagnostic, PluginRuntimeError::None);
            std::snprintf(name, sizeof(name), "register.%s.legal_control_order", violation.leaf);
            check_sequence(name, kSequenceUvStart);
            runtime.shutdown();
        }
        set_register_mode(RegisterMode::Ok);

        // ---------- 组 6：槽位边界与所有权 ----------
        // 63 合法：上界槽位能发布、能通过 get_service 取回同一张表、能真实调用
        set_slot63_probe(false);
        reset_sequence();
        diagnostic = runtime.start(kPlanSlot63);
        check_code("slot.slot63_start_ok", diagnostic, PluginRuntimeError::None);
        check_state("slot.slot63_ready", PluginRuntimeState::Ready);
        check_sequence("slot.slot63_order", kSequenceSlotStart);
        check_service_address("slot.slot63_published", static_cast<ServiceSlot>(63), &kMathTable);
        {
            const TestMathApi* api  = runtime.get_service<TestMathApi>(static_cast<ServiceSlot>(63));
            const int32_t      sum  = (api != nullptr) ? api->add(40, 2) : 0;
            check_int32("slot.slot63_indirect_call", sum, 42);
        }
        diagnostic = runtime.shutdown();
        check_code("slot.slot63_shutdown_ok", diagnostic, PluginRuntimeError::None);
        check_sequence("slot.slot63_full_order", kSequenceSlotFull);
        check_slots_empty("slot.slot63_slots_empty");

        // 64 拒绝且不索引：越界发布必须返回 ServiceSlotOutOfRange，诊断槽位就是 64
        set_slot63_probe(true);
        reset_sequence();
        diagnostic = runtime.start(kPlanSlot63);
        check_code("slot.slot64_rejected", diagnostic, PluginRuntimeError::ServiceSlotOutOfRange);
        check_slot_index("slot.slot64_reported_slot", diagnostic, 64);
        check_state("slot.slot64_stays_idle", PluginRuntimeState::Idle);
        check_error("slot.slot64_probe_code", slot63_probe_code(), PluginRuntimeError::ServiceSlotOutOfRange);
        check_bool("slot.slot64_leaves_slot63_empty", slot63_empty_after_probe(), true);
        check_sequence("slot.slot64_rollback_order", "E.r,E.u");
        check_slots_empty("slot.slot64_no_residual_slots");

        // 同环境合法对照：探测关闭后同一计划必须进入 Ready
        set_slot63_probe(false);
        reset_sequence();
        diagnostic = runtime.start(kPlanSlot63);
        check_code("slot.slot64_legal_control", diagnostic, PluginRuntimeError::None);
        check_state("slot.slot64_legal_control_ready", PluginRuntimeState::Ready);
        runtime.shutdown();

        // on_unload 注销别人的槽必须被拒绝，而且不能把 owner 的表清掉
        set_cross_revoke(true);
        reset_sequence();
        diagnostic = runtime.start(kPlanUv);
        check_code("slot.cross_revoke_start_ok", diagnostic, PluginRuntimeError::None);
        diagnostic = runtime.shutdown();
        check_code("slot.cross_revoke_shutdown_ok", diagnostic, PluginRuntimeError::None);
        check_state("slot.cross_revoke_idle", PluginRuntimeState::Idle);
        check_error("slot.cross_revoke_rejected", cross_revoke_code(), PluginRuntimeError::ServiceOwnerMismatch);
        check_bool("slot.cross_revoke_keeps_owner_table", cross_revoke_upstream_still_published(), true);
        check_slots_empty("slot.cross_revoke_slots_empty");
        set_cross_revoke(false);

        // Ready 期间发布/注销都必须拒绝，且不改变已发布的表
        reset_sequence();
        diagnostic = runtime.start(kPlanUv);
        check_code("slot.ready_start_ok", diagnostic, PluginRuntimeError::None);
        diagnostic = runtime.publish_service(2, &kAuxTable);
        check_code("slot.ready_publish_rejected", diagnostic, PluginRuntimeError::ServicePublishRejected);
        check_slot_index("slot.ready_publish_slot", diagnostic, 2);
        check_service_address("slot.ready_publish_keeps_table", ServiceSlot::Math, &kMathTable);
        diagnostic = runtime.revoke_service(2);
        check_code("slot.ready_revoke_rejected", diagnostic, PluginRuntimeError::ServiceRevokeRejected);
        check_service_address("slot.ready_revoke_keeps_table", ServiceSlot::Math, &kMathTable);
        diagnostic = runtime.shutdown();
        check_code("slot.ready_shutdown_ok", diagnostic, PluginRuntimeError::None);
        check_slots_empty("slot.ready_all_slots_empty");

        // ---------- 组 7：真实调用已选上游的公开函数 ----------
        set_register_mode(RegisterMode::Ok);
        reset_sequence();
        diagnostic = runtime.start(kPlanUv);
        check_code("upstream.start_ok", diagnostic, PluginRuntimeError::None);
        check_state("upstream.ready", PluginRuntimeState::Ready);
        check_sequence("upstream.start_order", kSequenceUvStart);
        // 下游 V 在 on_init 里通过槽 2 取表并调用：19 + 23 = 42、7 * 7 = 49
        check_bool("upstream.downstream_saw_service", upstream_service_seen(), true);
        check_int32("upstream.downstream_add_result", upstream_add_result(), 42);
        check_int32("upstream.downstream_square_result", upstream_square_result(), 49);
        check_service_address("upstream.slot_table_identity", ServiceSlot::Math, &kMathTable);
        {
            const TestMathApi* api    = runtime.get_service<TestMathApi>(ServiceSlot::Math);
            const int32_t      sum    = (api != nullptr) ? api->add(-5, 8) : 0;
            const int32_t      square = (api != nullptr) ? api->square(12) : 0;
            check_int32("upstream.host_add_result", sum, 3);
            check_int32("upstream.host_square_result", square, 144);
        }
        diagnostic = runtime.shutdown();
        check_code("upstream.shutdown_ok", diagnostic, PluginRuntimeError::None);
        check_sequence("upstream.full_order", kSequenceUvFull);
        check_slots_empty("upstream.slots_empty");
    }
} // namespace ID::Bench::Runtime
