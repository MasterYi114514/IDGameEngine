#include <cstdio>

#include "RuntimeCases.hpp"
#include "RuntimeFixture.hpp"

/**
 *  组 1/2/3/5：正常四阶段顺序、零插件、init 失败清理、状态机与重入
 *  1. 顺序断言用整条序列字符串相等，不依赖"看到了某个回调"这种弱判定；
 *  2. init 失败的三个位置各跑一次，失败后必须 shutdown 幂等、槽清空、能再次 start；
 *  3. 每个失败用例前后都有同环境合法对照（同一计划、同一夹具，只关掉违约开关）。
 */

namespace ID::Bench::Runtime
{
    namespace
    {
        // 组 3 的三个失败位置：拓扑序 A,B,C 的首/中/末
        struct InitFailCase
        {
            InitFailMode mode;
            const char*  leaf;
            const char*  sequence;
            uint16_t     plugin;
        };

        constexpr InitFailCase kInitFailCases[3] = {
            { InitFailMode::First,  "first",  "A.r,B.r,C.r,A.i,A.s,C.u,B.u,A.u",             0 },
            { InitFailMode::Middle, "middle", "A.r,B.r,C.r,A.i,B.i,B.s,A.s,C.u,B.u,A.u",      1 },
            { InitFailMode::Last,   "last",   "A.r,B.r,C.r,A.i,B.i,C.i,C.s,B.s,A.s,C.u,B.u,A.u", 2 }
        };

        /**
         *  @brief 零插件生命周期的探针体：start 必须进 Ready、shutdown 必须回 Idle、全程无回调
         *  @param plan 零插件计划视图（计数为 0）
         *  @param prefix 用例名前缀，用来区分"全空视图"与"仅 records 非空"两种形态
         *  @note 跑在独立进程里；shutdown 之前主动刷盘，使 abort 发生在 shutdown 时仍然留下
         *        已判定过的证据（否则 stdout/report 的块缓冲会一起丢失）
         */
        void run_zero_plugin_probe(const PluginRuntimePlanView& plan, const char* prefix) noexcept
        {
            char           name[96];
            PluginRuntime& runtime = PluginRuntime::instance();

            std::snprintf(name, sizeof(name), "%s.precondition_idle", prefix);
            check_state(name, PluginRuntimeState::Idle);

            reset_sequence();
            PluginRuntimeDiagnostic diagnostic = runtime.start(plan);
            std::snprintf(name, sizeof(name), "%s.start_ok", prefix);
            check_code(name, diagnostic, PluginRuntimeError::None);
            std::snprintf(name, sizeof(name), "%s.ready", prefix);
            check_state(name, PluginRuntimeState::Ready);
            std::snprintf(name, sizeof(name), "%s.no_callbacks", prefix);
            check_sequence(name, "");

            std::fflush(nullptr);

            diagnostic = runtime.shutdown();
            std::snprintf(name, sizeof(name), "%s.shutdown_ok", prefix);
            check_code(name, diagnostic, PluginRuntimeError::None);
            std::snprintf(name, sizeof(name), "%s.idle", prefix);
            check_state(name, PluginRuntimeState::Idle);
            std::snprintf(name, sizeof(name), "%s.shutdown_no_callbacks", prefix);
            check_sequence(name, "");
            std::snprintf(name, sizeof(name), "%s.slots_empty", prefix);
            check_slots_empty(name);
        }
    } // namespace

    void run_lifecycle_cases() noexcept
    {
        PluginRuntime& runtime = PluginRuntime::instance();

        // ---------- 组 1：正常四阶段：register 正序 -> init 正序 -> shutdown 逆序 -> unload 逆序 ----------
        reset_sequence();
        PluginRuntimeDiagnostic diagnostic = runtime.start(kPlanAbcd);
        check_code("lifecycle.start_ok", diagnostic, PluginRuntimeError::None);
        check_state("lifecycle.start_ready", PluginRuntimeState::Ready);
        // 拓扑序是 D,B,A,C：上游先启动、下游后启动，且严格按 topo_order 而不是 records 顺序
        check_sequence("lifecycle.register_init_order", kSequenceAbcdStart);

        diagnostic = runtime.shutdown();
        check_code("lifecycle.shutdown_ok", diagnostic, PluginRuntimeError::None);
        check_state("lifecycle.shutdown_idle", PluginRuntimeState::Idle);
        // 逆序关闭：下游先关闭、上游后关闭
        check_sequence("lifecycle.shutdown_unload_order", kSequenceAbcdFull);
        check_slots_empty("lifecycle.slots_empty_after_shutdown");

        // ---------- 组 2：零插件（显式空数组；全空视图见 ZeroNullShutdownProbe） ----------
        // 全空视图（records/topo_order/slot_owner/provisions 全为 nullptr）是 PluginRuntime.hpp
        // 明文允许的零插件形态；它过去会触发 shutdown() 的内部断言 abort，现在按契约进入主矩阵，
        // 期望值没有放宽：start 进 Ready、shutdown 回 Idle、全程零回调、槽表为空。
        // 仍在子进程里保留一次同形态探针（ZeroNullShutdownProbe），避免将来同类断言回归时
        // 一次 abort 抹掉整个主矩阵的结果。
        reset_sequence();
        diagnostic = runtime.start(kPlanZeroNull);
        check_code("zero.null_view_start_ok", diagnostic, PluginRuntimeError::None);
        check_state("zero.null_view_ready", PluginRuntimeState::Ready);
        check_sequence("zero.null_view_no_callbacks", "");
        diagnostic = runtime.shutdown();
        check_code("zero.null_view_shutdown_ok", diagnostic, PluginRuntimeError::None);
        check_state("zero.null_view_idle", PluginRuntimeState::Idle);
        check_sequence("zero.null_view_shutdown_no_callbacks", "");
        check_slots_empty("zero.null_view_slots_empty");

        reset_sequence();
        diagnostic = runtime.start(kPlanZeroExplicit);
        check_code("zero.empty_view_start_ok", diagnostic, PluginRuntimeError::None);
        check_state("zero.empty_view_ready", PluginRuntimeState::Ready);
        check_sequence("zero.empty_view_no_callbacks", "");
        diagnostic = runtime.shutdown();
        check_code("zero.empty_view_shutdown_ok", diagnostic, PluginRuntimeError::None);
        check_state("zero.empty_view_idle", PluginRuntimeState::Idle);
        check_sequence("zero.empty_view_shutdown_no_callbacks", "");
        check_slots_empty("zero.empty_view_slots_empty");

        // ---------- 组 3：init 失败（首个 / 中间 / 末尾） ----------
        // 同环境合法对照：同一计划、同一夹具，只把失败位置设为 None
        set_init_fail_mode(InitFailMode::None);
        reset_sequence();
        diagnostic = runtime.start(kPlanAbc);
        check_code("initfail.control_start_ok", diagnostic, PluginRuntimeError::None);
        check_sequence("initfail.control_order", kSequenceAbcStart);
        runtime.shutdown();
        check_sequence("initfail.control_full_order", kSequenceAbcFull);

        for (size_t i = 0; i < 3; ++i)
        {
            const InitFailCase& fail_case = kInitFailCases[i];
            char name[96];

            set_init_fail_mode(fail_case.mode);
            reset_sequence();
            diagnostic = runtime.start(kPlanAbc);

            std::snprintf(name, sizeof(name), "initfail.%s.code", fail_case.leaf);
            check_code(name, diagnostic, PluginRuntimeError::InitFailed);
            std::snprintf(name, sizeof(name), "initfail.%s.plugin", fail_case.leaf);
            check_plugin_index(name, diagnostic, fail_case.plugin);
            std::snprintf(name, sizeof(name), "initfail.%s.idle", fail_case.leaf);
            check_state(name, PluginRuntimeState::Idle);
            // 失败插件自己的 on_shutdown 必须被调用；已尝试 init 前缀逆序 shutdown；已注册全集逆序 unload
            std::snprintf(name, sizeof(name), "initfail.%s.rollback_order", fail_case.leaf);
            check_sequence(name, fail_case.sequence);
            std::snprintf(name, sizeof(name), "initfail.%s.slots_empty", fail_case.leaf);
            check_slots_empty(name);

            // 失败后 shutdown 必须幂等，且不再执行任何回调
            diagnostic = runtime.shutdown();
            std::snprintf(name, sizeof(name), "initfail.%s.shutdown_idempotent", fail_case.leaf);
            check_code(name, diagnostic, PluginRuntimeError::None);
            std::snprintf(name, sizeof(name), "initfail.%s.shutdown_no_callbacks", fail_case.leaf);
            check_sequence(name, fail_case.sequence);

            // 修正条件后再次 start 必须成功
            set_init_fail_mode(InitFailMode::None);
            reset_sequence();
            diagnostic = runtime.start(kPlanAbc);
            std::snprintf(name, sizeof(name), "initfail.%s.retry_ok", fail_case.leaf);
            check_code(name, diagnostic, PluginRuntimeError::None);
            std::snprintf(name, sizeof(name), "initfail.%s.retry_order", fail_case.leaf);
            check_sequence(name, kSequenceAbcStart);
            runtime.shutdown();
            std::snprintf(name, sizeof(name), "initfail.%s.retry_idle", fail_case.leaf);
            check_state(name, PluginRuntimeState::Idle);
        }

        // ---------- 组 5：重复 start / Idle shutdown / 回调重入 ----------
        // 失败后再次 start 已在组 3 覆盖；这里覆盖状态机幂等与回调内重入
        reset_sequence();
        diagnostic = runtime.start(kPlanAbcd);
        check_code("state.start_ok", diagnostic, PluginRuntimeError::None);
        check_sequence("state.start_order", kSequenceAbcdStart);

        diagnostic = runtime.start(kPlanAbcd);
        check_code("state.repeat_start_already_started", diagnostic, PluginRuntimeError::AlreadyStarted);
        check_state("state.repeat_start_still_ready", PluginRuntimeState::Ready);
        check_sequence("state.repeat_start_no_callbacks", kSequenceAbcdStart);

        diagnostic = runtime.shutdown();
        check_code("state.shutdown_ok", diagnostic, PluginRuntimeError::None);
        check_sequence("state.shutdown_order", kSequenceAbcdFull);

        diagnostic = runtime.shutdown();
        check_code("state.idle_shutdown_idempotent", diagnostic, PluginRuntimeError::None);
        check_state("state.idle_shutdown_stays_idle", PluginRuntimeState::Idle);
        check_sequence("state.idle_shutdown_no_callbacks", kSequenceAbcdFull);

        // 回调内重入：register 阶段调 start、init 阶段调 shutdown 都必须 Busy，且不改变流程
        set_reentry_plan(&kPlanAbcd);
        reset_sequence();
        diagnostic = runtime.start(kPlanAbcd);
        check_code("state.reentry_start_ok", diagnostic, PluginRuntimeError::None);
        check_state("state.reentry_ready", PluginRuntimeState::Ready);
        check_error("state.reentry_in_register_busy", reentry_register_code(), PluginRuntimeError::Busy);
        check_error("state.reentry_in_init_busy", reentry_init_code(), PluginRuntimeError::Busy);
        check_sequence("state.reentry_flow_unchanged", kSequenceAbcdStart);
        set_reentry_plan(nullptr);

        runtime.shutdown();
        check_state("state.reentry_shutdown_idle", PluginRuntimeState::Idle);
        check_sequence("state.reentry_shutdown_order", kSequenceAbcdFull);
        check_slots_empty("state.reentry_slots_empty");

        // 组 3/5 结束时所有开关必须回到合法值，否则后面的组会继承违约模式
        set_init_fail_mode(InitFailMode::None);
        set_register_mode(RegisterMode::Ok);
    }

    void ZeroNullShutdownProbe() noexcept
    {
        run_zero_plugin_probe(kPlanZeroNull, "zero.null_view");
    }

    void ZeroNullRecordsShutdownProbe() noexcept
    {
        run_zero_plugin_probe(kPlanZeroNullRecords, "zero.null_records_view");
    }
} // namespace ID::Bench::Runtime
