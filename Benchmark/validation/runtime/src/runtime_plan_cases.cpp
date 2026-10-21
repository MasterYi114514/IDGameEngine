#include <cstdio>

#include "benchmark_support.hpp"
#include "RuntimeCases.hpp"
#include "RuntimeFixture.hpp"

/**
 *  组 8：计划视图边界
 *  1. 每条用例只把 scratch 计划改一处，期望得到结构化 Plan* 错误，且不进入 Ready、不执行任何回调；
 *  2. 每条用例后面紧跟同环境合法对照（kPlanAbcd 完整生命周期），证明拒绝来自计划本身而不是环境；
 *  3. plugin_count 超限用例把数组指针全部置空：若实现先索引再校验，会读到空指针或越界。
 */

namespace ID::Bench::Runtime
{
    namespace
    {
        /**
         *  @brief 判定一条计划被拒绝：错误码相等、状态回到 Idle、没有执行任何回调、合法对照仍成立
         *  @param leaf 用例名叶子（拼进 plan.<leaf>.xxx）
         *  @param expected 期望的错误类别
         *  @return 原样返回 start 的诊断，供调用方继续断言 plugin/slot
         *  @note 计划由调用方在此之前用 reset_plan_* + 单点改写准备好
         */
        PluginRuntimeDiagnostic judge_plan_rejection(const char* leaf, PluginRuntimeError expected) noexcept
        {
            char name[96];

            reset_sequence();
            const PluginRuntimeDiagnostic diagnostic = PluginRuntime::instance().start(scratch_plan());

            std::snprintf(name, sizeof(name), "plan.%s.code", leaf);
            check_code(name, diagnostic, expected);
            std::snprintf(name, sizeof(name), "plan.%s.idle", leaf);
            check_state(name, PluginRuntimeState::Idle);
            std::snprintf(name, sizeof(name), "plan.%s.no_callbacks", leaf);
            check_sequence(name, "");
            std::snprintf(name, sizeof(name), "plan.%s.legal_control", leaf);
            ID::Bench::Check(legal_control_abcd(), name, "同环境 A-B-C-D 合法生命周期仍进入 Ready 并回到 Idle");
            return diagnostic;
        }
    } // namespace

    void run_plan_cases() noexcept
    {
        // 批前合法对照：证明本组开始时环境与夹具都正常
        check_bool("plan.control_before_batch", legal_control_abcd(), true);

        // ---- 计数 / ABI / 尺寸 ----
        // plugin_count 超限：数组指针全部为空，校验必须发生在任何数组访问之前
        reset_plan_abcd();
        scratch_plan().plugin_count = static_cast<uint16_t>(PluginRuntime::kMaxPlugins + 1);
        scratch_plan().records      = nullptr;
        scratch_plan().topo_order   = nullptr;
        scratch_plan().slot_owner   = nullptr;
        judge_plan_rejection("too_many_plugins", PluginRuntimeError::PlanTooManyPlugins);

        reset_plan_abcd();
        scratch_plan().abi_revision = kPluginRuntimeAbiRevision + 1;
        judge_plan_rejection("abi_mismatch", PluginRuntimeError::PlanAbiMismatch);

        reset_plan_abcd();
        scratch_plan().struct_size = static_cast<uint32_t>(sizeof(PluginRuntimePlanView)) - 1;
        judge_plan_rejection("struct_size_mismatch", PluginRuntimeError::PlanStructSizeMismatch);

        // ---- 拓扑序 ----
        reset_plan_abcd();
        scratch_topo()[1] = 0;
        {
            const PluginRuntimeDiagnostic diagnostic =
                judge_plan_rejection("topo_duplicate", PluginRuntimeError::PlanInvalidTopology);
            check_plugin_index("plan.topo_duplicate.plugin", diagnostic, 0);
        }

        reset_plan_abcd();
        scratch_topo()[0] = 4;
        {
            const PluginRuntimeDiagnostic diagnostic =
                judge_plan_rejection("topo_out_of_range", PluginRuntimeError::PlanInvalidTopology);
            check_plugin_index("plan.topo_out_of_range.plugin", diagnostic, 4);
        }

        // ---- 记录身份 ----
        reset_plan_abcd();
        scratch_records()[1].plugin_id = kRecordA.plugin_id;
        {
            const PluginRuntimeDiagnostic diagnostic =
                judge_plan_rejection("record_duplicate_id", PluginRuntimeError::PlanInvalidRecord);
            check_plugin_index("plan.record_duplicate_id.plugin", diagnostic, 1);
        }

        reset_plan_abcd();
        scratch_records()[0].plugin_id = 0;
        judge_plan_rejection("record_zero_id", PluginRuntimeError::PlanInvalidRecord);

        reset_plan_abcd();
        scratch_records()[0].name = "";
        judge_plan_rejection("record_empty_name", PluginRuntimeError::PlanInvalidRecord);

        reset_plan_abcd();
        scratch_records()[0].name = kLongRecordName;
        judge_plan_rejection("record_name_too_long", PluginRuntimeError::PlanInvalidRecord);

        // ---- 必需数组为空 ----
        reset_plan_abcd();
        scratch_plan().records = nullptr;
        judge_plan_rejection("records_null", PluginRuntimeError::PlanNullPointer);

        reset_plan_abcd();
        scratch_plan().provision_count = 1;
        scratch_plan().provisions      = nullptr;
        judge_plan_rejection("provisions_null", PluginRuntimeError::PlanNullPointer);

        // ---- provides 与 slot_owner 的一致性 ----
        reset_plan_uv();
        scratch_owners()[2] = 1;
        {
            const PluginRuntimeDiagnostic diagnostic =
                judge_plan_rejection("slot_owner_vs_provision", PluginRuntimeError::PlanInvalidSlotOwner);
            check_slot_index("plan.slot_owner_vs_provision.slot", diagnostic, 2);
        }

        reset_plan_uv();
        scratch_owners()[5] = 0;
        {
            const PluginRuntimeDiagnostic diagnostic =
                judge_plan_rejection("slot_owner_without_provision", PluginRuntimeError::PlanInvalidSlotOwner);
            check_slot_index("plan.slot_owner_without_provision.slot", diagnostic, 5);
        }

        reset_plan_uv();
        scratch_owners()[5] = 9;
        {
            const PluginRuntimeDiagnostic diagnostic =
                judge_plan_rejection("slot_owner_out_of_range", PluginRuntimeError::PlanInvalidSlotOwner);
            check_plugin_index("plan.slot_owner_out_of_range.plugin", diagnostic, 9);
        }

        reset_plan_uv();
        scratch_provisions()[0].slot = 64;
        {
            const PluginRuntimeDiagnostic diagnostic =
                judge_plan_rejection("provision_slot_64", PluginRuntimeError::PlanInvalidProvision);
            check_slot_index("plan.provision_slot_64.slot", diagnostic, 64);
        }

        reset_plan_uv();
        scratch_provisions()[0].plugin_index = 4;
        {
            const PluginRuntimeDiagnostic diagnostic =
                judge_plan_rejection("provision_owner_out_of_range", PluginRuntimeError::PlanInvalidProvision);
            check_plugin_index("plan.provision_owner_out_of_range.plugin", diagnostic, 4);
        }

        reset_plan_uv();
        scratch_provisions()[0].api_revision = 0;
        judge_plan_rejection("provision_revision_zero", PluginRuntimeError::PlanInvalidProvision);

        reset_plan_uv();
        scratch_provisions()[1].slot = 2;
        judge_plan_rejection("provision_duplicate_slot", PluginRuntimeError::PlanInvalidSlotOwner);

        // ---- 零插件产品不允许夹带 provides ----
        reset_plan_abcd();
        scratch_plan().plugin_count    = 0;
        scratch_plan().provision_count = 1;
        scratch_plan().provisions      = scratch_provisions();
        judge_plan_rejection("zero_plugins_with_provision", PluginRuntimeError::PlanInvalidProvision);

        // 批后合法对照：整组拒绝没有破坏运行期状态
        check_bool("plan.control_after_batch", legal_control_abcd(), true);
    }
} // namespace ID::Bench::Runtime
