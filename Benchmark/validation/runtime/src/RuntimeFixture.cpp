#include "RuntimeFixture.hpp"

#include <cstdio>
#include <cstring>
#include <string_view>

#include "benchmark_support.hpp"

/**
 *  运行期 PluginRuntime 校验夹具实现
 *  1. 序列缓冲、模式开关与观测结果都是具有静态存储期的 POD/枚举，全部常量初始化，
 *     没有运行期全局构造函数（项目红线）；
 *  2. 断言辅助只把"期望值 / 实际值"写进 detail，判定本身是真实条件比较；
 *  3. scratch 计划用于计划边界用例：静态存储期，逐用例复位后只改一处。
 */

namespace ID::Bench::Runtime
{
    namespace
    {
        constexpr size_t kSlotCount = PluginRuntime::kMaxSlots;

        constinit char   g_sequence_text[kSequenceCapacity] = {};
        constinit size_t g_sequence_length                  = 0;

        // 逐用例模式开关：每次生命周期前由用例设置，回调只读
        constinit InitFailMode                 g_init_fail_mode = InitFailMode::None;
        constinit RegisterMode                 g_register_mode  = RegisterMode::Ok;
        constinit bool                         g_cross_revoke   = false;
        constinit bool                         g_slot63_probe   = false;
        constinit const PluginRuntimePlanView* g_reentry_plan   = nullptr;

        // 回调内观测到的结果，用例结束后原地断言（不在回调里断言：回调里只能记录）
        constinit PluginRuntimeError g_reentry_register_code    = PluginRuntimeError::None;
        constinit PluginRuntimeError g_reentry_init_code        = PluginRuntimeError::None;
        constinit PluginRuntimeError g_cross_revoke_code        = PluginRuntimeError::None;
        constinit bool               g_cross_revoke_still_held  = false;
        constinit bool               g_upstream_seen            = false;
        constinit int32_t            g_upstream_add             = 0;
        constinit int32_t            g_upstream_square          = 0;
        constinit PluginRuntimeError g_slot63_probe_code        = PluginRuntimeError::None;
        constinit bool               g_slot63_empty_after_probe = false;
        constinit PluginRuntimeError g_out_of_range_probe_code  = PluginRuntimeError::None;

        // 断言 detail 的格式化缓冲：只在冷路径同步使用，Check 会立刻复制内容
        constinit char g_detail_text[256] = {};

        // scratch 计划：逐用例复位后只改一处，仍然是静态存储期对象
        constinit PluginRuntimePlanView  g_plan_scratch                = kPlanAbcd;
        constinit PluginRuntimeRecord    g_records_scratch[4]          = {};
        constinit uint16_t               g_topo_scratch[4]             = {};
        constinit uint16_t               g_owners_scratch[kSlotCount]  = {};
        constinit PluginRuntimeProvision g_provisions_scratch[2]       = {};
    } // namespace

    int32_t test_api_add(int32_t lhs, int32_t rhs) noexcept
    {
        return lhs + rhs;
    }

    int32_t test_api_square(int32_t value) noexcept
    {
        return value * value;
    }

    int32_t test_api_scale(int32_t value, int32_t factor) noexcept
    {
        return value * factor;
    }

    void A_on_register() noexcept
    {
        append_sequence("A.r");
        if (g_reentry_plan != nullptr)
        {
            g_reentry_register_code = PluginRuntime::instance().start(*g_reentry_plan).code;
        }
    }

    bool A_on_init() noexcept
    {
        append_sequence("A.i");
        return g_init_fail_mode != InitFailMode::First;
    }

    void A_on_shutdown() noexcept
    {
        append_sequence("A.s");
    }

    void A_on_unload() noexcept
    {
        append_sequence("A.u");
    }

    void B_on_register() noexcept
    {
        append_sequence("B.r");
    }

    bool B_on_init() noexcept
    {
        append_sequence("B.i");
        if (g_reentry_plan != nullptr)
        {
            g_reentry_init_code = PluginRuntime::instance().shutdown().code;
        }
        return g_init_fail_mode != InitFailMode::Middle;
    }

    void B_on_shutdown() noexcept
    {
        append_sequence("B.s");
    }

    void B_on_unload() noexcept
    {
        append_sequence("B.u");
    }

    void C_on_register() noexcept
    {
        append_sequence("C.r");
    }

    bool C_on_init() noexcept
    {
        append_sequence("C.i");
        return g_init_fail_mode != InitFailMode::Last;
    }

    void C_on_shutdown() noexcept
    {
        append_sequence("C.s");
    }

    void C_on_unload() noexcept
    {
        append_sequence("C.u");
    }

    void D_on_register() noexcept
    {
        append_sequence("D.r");
    }

    bool D_on_init() noexcept
    {
        append_sequence("D.i");
        return true;
    }

    void D_on_shutdown() noexcept
    {
        append_sequence("D.s");
    }

    void D_on_unload() noexcept
    {
        append_sequence("D.u");
    }

    void U_on_register() noexcept
    {
        append_sequence("U.r");

        PluginRuntime& runtime = PluginRuntime::instance();
        switch (g_register_mode)
        {
            case RegisterMode::Ok:
                runtime.publish_service(2, &kMathTable);
                break;
            case RegisterMode::Missing:
                break;                                      // 计划里声明了槽 2，但故意不发布
            case RegisterMode::WrongOwner:
                runtime.publish_service(3, &kAuxTable);      // 侵占 V 的槽，必须被拒绝
                runtime.publish_service(2, &kMathTable);     // 自己的槽仍然发布成功
                break;
            case RegisterMode::Duplicate:
                runtime.publish_service(2, &kMathTable);
                runtime.publish_service(2, &kMathTable);     // 同槽重复发布，必须被拒绝
                break;
            case RegisterMode::OutOfRange:
                g_out_of_range_probe_code = runtime.publish_service(64, &kAuxTable).code;
                runtime.publish_service(2, &kMathTable);
                break;
        }
    }

    bool U_on_init() noexcept
    {
        append_sequence("U.i");
        return true;
    }

    void U_on_shutdown() noexcept
    {
        append_sequence("U.s");
    }

    void U_on_unload() noexcept
    {
        append_sequence("U.u");
        PluginRuntime::instance().revoke_service(2);
    }

    void V_on_register() noexcept
    {
        append_sequence("V.r");
        PluginRuntime::instance().publish_service(3, &kAuxTable);
    }

    bool V_on_init() noexcept
    {
        append_sequence("V.i");

        // 下游在 on_init 里真正调用上游通过槽位公开的函数表
        const TestMathApi* api = PluginRuntime::instance().get_service<TestMathApi>(ServiceSlot::Math);
        g_upstream_seen = (api != nullptr);
        if (api != nullptr)
        {
            g_upstream_add    = api->add(19, 23);
            g_upstream_square = api->square(7);
        }
        return true;
    }

    void V_on_shutdown() noexcept
    {
        append_sequence("V.s");
    }

    void V_on_unload() noexcept
    {
        append_sequence("V.u");

        PluginRuntime& runtime = PluginRuntime::instance();
        if (g_cross_revoke)
        {
            // V 先于 U 卸载（拓扑逆序）：此刻 U 的槽仍然发布着，
            // 注销别人的槽必须被拒绝，而且不能把表清掉
            g_cross_revoke_code       = runtime.revoke_service(2).code;
            g_cross_revoke_still_held = (runtime.get_service<TestMathApi>(ServiceSlot::Math) != nullptr);
        }
        runtime.revoke_service(3);
    }

    void E_on_register() noexcept
    {
        append_sequence("E.r");

        PluginRuntime& runtime = PluginRuntime::instance();
        if (g_slot63_probe)
        {
            // 槽 64 必须在数组下标之前被拒绝：探测后 63 号槽仍未发布
            g_slot63_probe_code        = runtime.publish_service(64, &kAuxTable).code;
            g_slot63_empty_after_probe = (runtime.get_service<TestMathApi>(static_cast<ServiceSlot>(63)) == nullptr);
        }
        runtime.publish_service(63, &kMathTable);
    }

    bool E_on_init() noexcept
    {
        append_sequence("E.i");
        return true;
    }

    void E_on_shutdown() noexcept
    {
        append_sequence("E.s");
    }

    void E_on_unload() noexcept
    {
        append_sequence("E.u");
        PluginRuntime::instance().revoke_service(63);
    }

    void N_on_register() noexcept
    {
        // 无槽位能力：注册期不发布任何函数表（发布反而会因为 slot_owner 与计划不符被拒绝）
        append_sequence("N.r");
    }

    bool N_on_init() noexcept
    {
        append_sequence("N.i");
        return true;
    }

    void N_on_shutdown() noexcept
    {
        append_sequence("N.s");
    }

    void N_on_unload() noexcept
    {
        // 没有服务表可注销
        append_sequence("N.u");
    }

    void reset_sequence() noexcept
    {
        g_sequence_length = 0;
        g_sequence_text[0] = '\0';
    }

    void append_sequence(const char* token) noexcept
    {
        const size_t token_length = std::strlen(token);
        const size_t separator    = (g_sequence_length != 0) ? 1 : 0;
        if (g_sequence_length + separator + token_length + 1 > kSequenceCapacity)
        {
            return;     // 容量不足就不写：序列会被截断，比较必然失败，不会被当成通过
        }
        if (separator != 0)
        {
            g_sequence_text[g_sequence_length] = ',';
            ++g_sequence_length;
        }
        std::memcpy(g_sequence_text + g_sequence_length, token, token_length);
        g_sequence_length += token_length;
        g_sequence_text[g_sequence_length] = '\0';
    }

    const char* sequence_text() noexcept
    {
        return g_sequence_text;
    }

    bool sequence_equals(const char* expected) noexcept
    {
        return std::strcmp(g_sequence_text, expected) == 0;
    }

    void check_sequence(const char* case_name, const char* expected) noexcept
    {
        if (sequence_equals(expected))
        {
            ID::Bench::Check(true, case_name, expected);
            return;
        }
        std::snprintf(g_detail_text, sizeof(g_detail_text), "expect=%s actual=%s", expected, g_sequence_text);
        ID::Bench::Check(false, case_name, g_detail_text);
    }

    void check_code(const char* case_name, const PluginRuntimeDiagnostic& diagnostic, PluginRuntimeError expected) noexcept
    {
        const std::string_view expected_text = plugin_runtime_error_name(expected);
        const std::string_view actual_text   = plugin_runtime_error_name(diagnostic.code);
        const std::string_view state_text    = plugin_runtime_state_name(diagnostic.state);
        std::snprintf(g_detail_text, sizeof(g_detail_text), "expect=%s actual=%s at=%s plugin=%u slot=%d",
            expected_text.data(), actual_text.data(), state_text.data(),
            static_cast<unsigned>(diagnostic.plugin), static_cast<int>(diagnostic.slot));
        ID::Bench::Check(diagnostic.code == expected, case_name, g_detail_text);
    }

    void check_error(const char* case_name, PluginRuntimeError actual, PluginRuntimeError expected) noexcept
    {
        const std::string_view expected_text = plugin_runtime_error_name(expected);
        const std::string_view actual_text   = plugin_runtime_error_name(actual);
        std::snprintf(g_detail_text, sizeof(g_detail_text), "expect=%s actual=%s",
            expected_text.data(), actual_text.data());
        ID::Bench::Check(actual == expected, case_name, g_detail_text);
    }

    void check_state(const char* case_name, PluginRuntimeState expected) noexcept
    {
        const PluginRuntimeState actual       = PluginRuntime::instance().state();
        const std::string_view   expected_text = plugin_runtime_state_name(expected);
        const std::string_view   actual_text   = plugin_runtime_state_name(actual);
        std::snprintf(g_detail_text, sizeof(g_detail_text), "expect=%s actual=%s",
            expected_text.data(), actual_text.data());
        ID::Bench::Check(actual == expected, case_name, g_detail_text);
    }

    void check_plugin_index(const char* case_name, const PluginRuntimeDiagnostic& diagnostic, uint16_t expected) noexcept
    {
        std::snprintf(g_detail_text, sizeof(g_detail_text), "expect=%u actual=%u",
            static_cast<unsigned>(expected), static_cast<unsigned>(diagnostic.plugin));
        ID::Bench::Check(diagnostic.plugin == expected, case_name, g_detail_text);
    }

    void check_slot_index(const char* case_name, const PluginRuntimeDiagnostic& diagnostic, int16_t expected) noexcept
    {
        std::snprintf(g_detail_text, sizeof(g_detail_text), "expect=%d actual=%d",
            static_cast<int>(expected), static_cast<int>(diagnostic.slot));
        ID::Bench::Check(diagnostic.slot == expected, case_name, g_detail_text);
    }

    void check_slots_empty(const char* case_name) noexcept
    {
        size_t published = 0;
        for (size_t slot = 0; slot < kSlotCount; ++slot)
        {
            if (PluginRuntime::instance().get_service<void>(static_cast<ServiceSlot>(slot)) != nullptr)
            {
                ++published;
            }
        }
        std::snprintf(g_detail_text, sizeof(g_detail_text), "non_null_slots=%zu expect=0", published);
        ID::Bench::Check(published == 0, case_name, g_detail_text);
    }

    void check_service_address(const char* case_name, ServiceSlot slot, const void* expected) noexcept
    {
        const void* actual = PluginRuntime::instance().get_service<void>(slot);
        std::snprintf(g_detail_text, sizeof(g_detail_text), "expect=%p actual=%p slot=%u",
            expected, actual, static_cast<unsigned>(slot));
        ID::Bench::Check(actual == expected, case_name, g_detail_text);
    }

    void check_int32(const char* case_name, int32_t actual, int32_t expected) noexcept
    {
        std::snprintf(g_detail_text, sizeof(g_detail_text), "expect=%d actual=%d",
            static_cast<int>(expected), static_cast<int>(actual));
        ID::Bench::Check(actual == expected, case_name, g_detail_text);
    }

    void check_bool(const char* case_name, bool actual, bool expected) noexcept
    {
        std::snprintf(g_detail_text, sizeof(g_detail_text), "expect=%d actual=%d",
            expected ? 1 : 0, actual ? 1 : 0);
        ID::Bench::Check(actual == expected, case_name, g_detail_text);
    }

    bool legal_control_abcd() noexcept
    {
        PluginRuntime& runtime = PluginRuntime::instance();
        reset_sequence();
        if (!runtime.start(kPlanAbcd).is_ok())
        {
            return false;
        }
        if (runtime.state() != PluginRuntimeState::Ready)
        {
            return false;
        }
        if (!runtime.shutdown().is_ok())
        {
            return false;
        }
        return runtime.state() == PluginRuntimeState::Idle;
    }

    void set_init_fail_mode(InitFailMode mode) noexcept
    {
        g_init_fail_mode = mode;
    }

    void set_register_mode(RegisterMode mode) noexcept
    {
        g_register_mode = mode;
    }

    void set_cross_revoke(bool enabled) noexcept
    {
        g_cross_revoke = enabled;
    }

    void set_slot63_probe(bool enabled) noexcept
    {
        g_slot63_probe = enabled;
    }

    void set_reentry_plan(const PluginRuntimePlanView* plan) noexcept
    {
        g_reentry_plan = plan;
    }

    PluginRuntimeError reentry_register_code() noexcept
    {
        return g_reentry_register_code;
    }

    PluginRuntimeError reentry_init_code() noexcept
    {
        return g_reentry_init_code;
    }

    PluginRuntimeError cross_revoke_code() noexcept
    {
        return g_cross_revoke_code;
    }

    bool cross_revoke_upstream_still_published() noexcept
    {
        return g_cross_revoke_still_held;
    }

    bool upstream_service_seen() noexcept
    {
        return g_upstream_seen;
    }

    int32_t upstream_add_result() noexcept
    {
        return g_upstream_add;
    }

    int32_t upstream_square_result() noexcept
    {
        return g_upstream_square;
    }

    PluginRuntimeError slot63_probe_code() noexcept
    {
        return g_slot63_probe_code;
    }

    bool slot63_empty_after_probe() noexcept
    {
        return g_slot63_empty_after_probe;
    }

    PluginRuntimeError out_of_range_probe_code() noexcept
    {
        return g_out_of_range_probe_code;
    }

    PluginRuntimePlanView& scratch_plan() noexcept
    {
        return g_plan_scratch;
    }

    PluginRuntimeRecord* scratch_records() noexcept
    {
        return g_records_scratch;
    }

    uint16_t* scratch_topo() noexcept
    {
        return g_topo_scratch;
    }

    uint16_t* scratch_owners() noexcept
    {
        return g_owners_scratch;
    }

    PluginRuntimeProvision* scratch_provisions() noexcept
    {
        return g_provisions_scratch;
    }

    void reset_plan_abcd() noexcept
    {
        for (size_t i = 0; i < 4; ++i)
        {
            g_records_scratch[i] = kRecordsAbcd[i];
            g_topo_scratch[i]    = kTopoDbac[i];
        }
        for (size_t slot = 0; slot < kSlotCount; ++slot)
        {
            g_owners_scratch[slot] = PluginCompileTime::kNoIndex;
        }
        g_provisions_scratch[0] = PluginRuntimeProvision{};
        g_provisions_scratch[1] = PluginRuntimeProvision{};

        g_plan_scratch                 = kPlanAbcd;
        g_plan_scratch.records         = g_records_scratch;
        g_plan_scratch.topo_order      = g_topo_scratch;
        g_plan_scratch.slot_owner      = g_owners_scratch;
        g_plan_scratch.provisions      = nullptr;
        g_plan_scratch.provision_count = 0;
    }

    void reset_plan_uv() noexcept
    {
        for (size_t i = 0; i < 2; ++i)
        {
            g_records_scratch[i] = kRecordsUv[i];
            g_topo_scratch[i]    = kTopoUv[i];
        }
        for (size_t slot = 0; slot < kSlotCount; ++slot)
        {
            g_owners_scratch[slot] = PluginCompileTime::kNoIndex;
        }
        g_owners_scratch[2] = 0;
        g_owners_scratch[3] = 1;

        for (size_t i = 0; i < 2; ++i)
        {
            g_provisions_scratch[i] = kProvisionUv[i];
        }

        g_plan_scratch            = kPlanUv;
        g_plan_scratch.records    = g_records_scratch;
        g_plan_scratch.topo_order = g_topo_scratch;
        g_plan_scratch.slot_owner = g_owners_scratch;
        g_plan_scratch.provisions = g_provisions_scratch;
    }
} // namespace ID::Bench::Runtime
