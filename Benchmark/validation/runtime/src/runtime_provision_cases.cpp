#include <cstddef>
#include <cstdio>

#include "benchmark_support.hpp"
#include "RuntimeCases.hpp"
#include "RuntimeFixture.hpp"

/**
 *  组 9：无槽位能力（PluginCompileTime::kNoSlot）与有槽/无槽混合 provision
 *  1. 计划书 11.1 / 12.2：纯头文件能力没有运行期函数表，注册期不发槽，
 *     "注册后必须已发布"的校验不得把它当成缺失槽位——否则任何带无槽位能力的合法产品都起不来；
 *  2. 混合 provision（同一计划里既有 kNoSlot 又有槽 2）必须同时满足两种语义：
 *     无槽位那条不计入发布检查，有槽那条仍然逐项核对发布与所有权；
 *  3. 反向一条：slot 为 kNoSlot 之外的负值（-2）仍然非法，必须返回 PlanInvalidProvision 且不索引数组；
 *     再加一条：无槽位能力不得在 slot_owner 里凭空占位。每条反向用例后紧跟同环境合法对照。
 */

namespace ID::Bench::Runtime
{
    namespace
    {
        /**
         *  @brief 判定一条 provision 计划被拒绝：错误码相等、状态回 Idle、零回调、合法对照仍成立
         *  @param leaf 用例名叶子（拼进 provision.<leaf>.xxx）
         *  @param expected 期望的错误类别
         *  @return 原样返回 start 的诊断，供调用方继续断言 slot
         *  @note 计划由调用方在此之前用 reset_plan_uv / reset_plan_abcd + 单点改写准备好
         */
        PluginRuntimeDiagnostic judge_provision_rejection(const char* leaf, PluginRuntimeError expected) noexcept
        {
            char name[96];

            reset_sequence();
            const PluginRuntimeDiagnostic diagnostic = PluginRuntime::instance().start(scratch_plan());

            std::snprintf(name, sizeof(name), "provision.%s.code", leaf);
            check_code(name, diagnostic, expected);
            std::snprintf(name, sizeof(name), "provision.%s.idle", leaf);
            check_state(name, PluginRuntimeState::Idle);
            std::snprintf(name, sizeof(name), "provision.%s.no_callbacks", leaf);
            check_sequence(name, "");
            std::snprintf(name, sizeof(name), "provision.%s.legal_control", leaf);
            ID::Bench::Check(legal_control_abcd(), name, "同环境 A-B-C-D 合法生命周期仍进入 Ready 并回到 Idle");
            return diagnostic;
        }
    } // namespace

    void run_provision_cases() noexcept
    {
        PluginRuntime& runtime = PluginRuntime::instance();

        // ---------- 9.1 只有无槽位能力：start -> Ready、shutdown -> Idle、不发任何槽 ----------
        reset_sequence();
        PluginRuntimeDiagnostic diagnostic = runtime.start(kPlanNoSlot);
        check_code("provision.noslot_start_ok", diagnostic, PluginRuntimeError::None);
        check_state("provision.noslot_ready", PluginRuntimeState::Ready);
        check_sequence("provision.noslot_start_order", kSequenceNoSlotStart);
        // 无槽位能力不发函数表：64 个槽必须全空，且 N 的注册不能被判成"缺失发布"
        check_slots_empty("provision.noslot_slots_stay_empty");

        diagnostic = runtime.shutdown();
        check_code("provision.noslot_shutdown_ok", diagnostic, PluginRuntimeError::None);
        check_state("provision.noslot_idle", PluginRuntimeState::Idle);
        check_sequence("provision.noslot_full_order", kSequenceNoSlotFull);
        check_slots_empty("provision.noslot_slots_empty_after_shutdown");

        // ---------- 9.2 混合 provision：无槽位能力 + 槽 2 同时存在 ----------
        reset_sequence();
        diagnostic = runtime.start(kPlanMixed);
        check_code("provision.mixed_start_ok", diagnostic, PluginRuntimeError::None);
        check_state("provision.mixed_ready", PluginRuntimeState::Ready);
        check_sequence("provision.mixed_start_order", kSequenceMixedStart);
        // 有槽那条照旧发布并被取回同一张表；无槽那条不产生任何槽
        check_service_address("provision.mixed_slot2_published", ServiceSlot::Math, &kMathTable);
        {
            const TestMathApi* api = runtime.get_service<TestMathApi>(ServiceSlot::Math);
            const int32_t      sum = (api != nullptr) ? api->add(20, 22) : 0;
            check_int32("provision.mixed_slot2_indirect_call", sum, 42);
        }
        {
            // slot_owner 里恰好只有槽 2 一个 owner（下标 1 = U），其余 63 槽都是 kNoIndex：
            // 无槽位能力不得在槽归属表里凭空占位
            size_t owned            = 0;
            size_t owner_is_u       = 0;
            for (size_t slot = 0; slot < PluginRuntime::kMaxSlots; ++slot)
            {
                const uint16_t owner = kOwnersMixed.cells[slot];
                if (owner != PluginCompileTime::kNoIndex)
                {
                    ++owned;
                    if (slot == 2 && owner == 1)
                    {
                        ++owner_is_u;
                    }
                }
            }
            check_int32("provision.mixed_slot_owner_count", static_cast<int32_t>(owned), 1);
            check_int32("provision.mixed_slot_owner_is_slot_provider", static_cast<int32_t>(owner_is_u), 1);
        }

        diagnostic = runtime.shutdown();
        check_code("provision.mixed_shutdown_ok", diagnostic, PluginRuntimeError::None);
        check_state("provision.mixed_idle", PluginRuntimeState::Idle);
        check_sequence("provision.mixed_full_order", kSequenceMixedFull);
        check_slots_empty("provision.mixed_slots_empty");

        // ---------- 9.3 反向：kNoSlot 之外的负槽位仍然非法 ----------
        // 被测实现在预检里必须"先接受 kNoSlot 哨兵、再按有符号范围判其余值"：
        // 只接受 -1 而放行 -2 会让 slot_owner 的位图索引变成 UB，所以这条必须被拒绝
        reset_plan_uv();
        scratch_provisions()[0].slot = -2;
        {
            const PluginRuntimeDiagnostic rejection =
                judge_provision_rejection("minus_two_slot", PluginRuntimeError::PlanInvalidProvision);
            check_slot_index("provision.minus_two_slot.reported_slot", rejection, -2);
        }

        // ---------- 9.4 反向：无槽位能力不得在 slot_owner 里凭空占位 ----------
        // 计划里只有一条 kNoSlot 能力，却在槽 7 写了 owner：槽归属与 provides 不一致，必须拒绝
        reset_plan_abcd();
        scratch_records()[0] = kRecordN;
        scratch_topo()[0]    = 0;               // 只留一个插件：拓扑序必须是 0 号下标
        scratch_plan().plugin_count    = 1;
        scratch_plan().provision_count = 1;
        scratch_provisions()[0] = PluginRuntimeProvision{
            .capability = "rt.headonly", .api_revision = 1, .plugin_index = 0, .slot = kNoSlot
        };
        scratch_plan().provisions = scratch_provisions();
        scratch_owners()[7]       = 0;
        {
            const PluginRuntimeDiagnostic rejection =
                judge_provision_rejection("noslot_owner_without_slot", PluginRuntimeError::PlanInvalidSlotOwner);
            check_slot_index("provision.noslot_owner_without_slot.reported_slot", rejection, 7);
        }

        // 批后合法对照：两条反向用例没有破坏运行期状态，两种无槽位计划仍能跑完
        reset_sequence();
        diagnostic = runtime.start(kPlanNoSlot);
        check_code("provision.control_after_batch_noslot", diagnostic, PluginRuntimeError::None);
        diagnostic = runtime.shutdown();
        check_code("provision.control_after_batch_shutdown", diagnostic, PluginRuntimeError::None);
        check_state("provision.control_after_batch_idle", PluginRuntimeState::Idle);
    }
} // namespace ID::Bench::Runtime
