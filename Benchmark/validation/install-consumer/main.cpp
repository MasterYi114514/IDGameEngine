#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "IDGameEngine.hpp"

/**
 *  安装消费校验程序（Benchmark 校验资产：不是产品，也不是 Benchmark 测试域）
 *  1. 只经安装前缀里的公开头消费产品：`IDGameEngine.hpp` 加按身份宏选中的插件能力头，
 *     不包含任何源码树路径、不链接任何源码树目标；
 *  2. 身份宏一致性在**预处理期**断言：产品注入的 `ID_USE_PLUGIN_*` 必须与 profile 的选中集合逐项一致，
 *     漏注入与全量注入都直接编译失败（计划书 1.1 的"三处必须一致"里第二处）；
 *  3. 数学 backend 的选择只发生在预处理期：`A+B` 产品必须由消费方显式给出
 *     `-DID_CONSUMER_MATH_BACKEND=scalar|simd`，没有运行期分支、没有函数指针搜索、没有能力探测；
 *  4. 运行期只验证生命周期与服务表：start -> (Ready) -> 读服务表 / 写一行日志 / 算一次数学
 *     -> shutdown -> 再次 shutdown（幂等）；
 *  5. 输出只走 stdout，不写 report.txt，也不参与任何性能评级。
 *
 *  由消费工程（同目录 CMakeLists.txt）注入的编译期期望：
 *    ID_CONSUMER_EXPECT_IDLOG / ID_CONSUMER_EXPECT_MATH_SCALAR / ID_CONSUMER_EXPECT_MATH_SIMD
 *    ID_CONSUMER_MATH_BACKEND_SCALAR | ID_CONSUMER_MATH_BACKEND_SIMD（仅 A+B 产品需要）
 */

// =============================================================================
// 1. 身份宏一致性：产品实际注入的身份宏必须与 profile 的选中集合逐项相同
// =============================================================================

#if defined(ID_CONSUMER_EXPECT_IDLOG)
    #if !defined(ID_USE_PLUGIN_IDLOG)
        #error "产品声明选中 ID.Log，但消费 TU 没有拿到 ID_USE_PLUGIN_IDLOG：PUBLIC 边或身份宏缺失"
    #endif
#else
    #if defined(ID_USE_PLUGIN_IDLOG)
        #error "产品未选中 ID.Log，但消费 TU 拿到了 ID_USE_PLUGIN_IDLOG：身份宏被全量注入"
    #endif
#endif

#if defined(ID_CONSUMER_EXPECT_MATH_SCALAR)
    #if !defined(ID_USE_PLUGIN_IDMATHSCALAR)
        #error "产品声明选中 ID.Math.Scalar，但消费 TU 没有拿到 ID_USE_PLUGIN_IDMATHSCALAR"
    #endif
#else
    #if defined(ID_USE_PLUGIN_IDMATHSCALAR)
        #error "产品未选中 ID.Math.Scalar，但消费 TU 拿到了 ID_USE_PLUGIN_IDMATHSCALAR"
    #endif
#endif

#if defined(ID_CONSUMER_EXPECT_MATH_SIMD)
    #if !defined(ID_USE_PLUGIN_IDMATHSIMD)
        #error "产品声明选中 ID.Math.SIMD，但消费 TU 没有拿到 ID_USE_PLUGIN_IDMATHSIMD"
    #endif
#else
    #if defined(ID_USE_PLUGIN_IDMATHSIMD)
        #error "产品未选中 ID.Math.SIMD，但消费 TU 拿到了 ID_USE_PLUGIN_IDMATHSIMD"
    #endif
#endif

// =============================================================================
// 2. 数学 backend 的编译期选择：A only / B only 由身份宏决定，A+B 必须显式选一个
// =============================================================================

#if defined(ID_USE_PLUGIN_IDMATHSCALAR) && defined(ID_USE_PLUGIN_IDMATHSIMD)

    // A+B 产品：两个身份宏都会出现，选择不得依赖宏的出现顺序，必须由消费方显式给出
    #if defined(ID_CONSUMER_MATH_BACKEND_SCALAR) && defined(ID_CONSUMER_MATH_BACKEND_SIMD)
        #error "同时给了 scalar 与 simd 两个选择：A+B 产品只能显式选一个 backend"
    #elif defined(ID_CONSUMER_MATH_BACKEND_SCALAR)
        #define ID_CONSUMER_MATH_IS_SCALAR 1
    #elif defined(ID_CONSUMER_MATH_BACKEND_SIMD)
        #define ID_CONSUMER_MATH_IS_SIMD 1
    #else
        #error "产品同时选中 ID.Math.Scalar 与 ID.Math.SIMD：consumer 必须用 -DID_CONSUMER_MATH_BACKEND=scalar|simd 在编译期显式选择实现（不得靠运行期分支）"
    #endif

#elif defined(ID_USE_PLUGIN_IDMATHSCALAR)

    #if defined(ID_CONSUMER_MATH_BACKEND_SIMD)
        #error "产品只选中 ID.Math.Scalar：不能选择 SIMD backend"
    #endif
    #define ID_CONSUMER_MATH_IS_SCALAR 1

#elif defined(ID_USE_PLUGIN_IDMATHSIMD)

    #if defined(ID_CONSUMER_MATH_BACKEND_SCALAR)
        #error "产品只选中 ID.Math.SIMD：不能选择 Scalar backend"
    #endif
    #define ID_CONSUMER_MATH_IS_SIMD 1

#endif

// =============================================================================
// 3. 能力头：只在身份宏成立时才包含（同一份源码要能在三种组合的产品里编译）
// =============================================================================

#if defined(ID_USE_PLUGIN_IDLOG)
    #include "IDLogApi.hpp"
#endif

#if defined(ID_CONSUMER_MATH_IS_SCALAR)
    #include "IDMathScalarApi.hpp"
#elif defined(ID_CONSUMER_MATH_IS_SIMD)
    #include "IDMathSIMDApi.hpp"
#endif

namespace
{
    /**
     *  @brief 打印一条检查结果并累计失败数（冷路径：只在消费校验里调用）
     *  @param failures 失败计数器
     *  @param condition 检查结果
     *  @param what 检查项的可读描述
     */
    void check(int& failures, bool condition, std::string_view what) noexcept
    {
        std::printf("[%s] %.*s\n",
            condition ? "PASS" : "FAIL",
            static_cast<int>(what.size()), what.data());
        if (!condition)
        {
            ++failures;
        }
    }

    /**
     *  @brief 把结构化诊断打印成一行（冷路径：只在启动/关闭失败时调用）
     *  @param stage 失败发生的动作名
     *  @param diagnostic runtime 返回的诊断
     */
    void report_failure(std::string_view stage, const ID::PluginRuntimeDiagnostic& diagnostic) noexcept
    {
        const std::string_view code  = ID::plugin_runtime_error_name(diagnostic.code);
        const std::string_view state = ID::plugin_runtime_state_name(diagnostic.state);
        std::printf("[FAIL] %.*s code=%.*s state=%.*s slot=%d\n",
            static_cast<int>(stage.size()), stage.data(),
            static_cast<int>(code.size()), code.data(),
            static_cast<int>(state.size()), state.data(),
            static_cast<int>(diagnostic.slot));
    }
} // namespace

int main()
{
    using ID::PluginRuntime;
    using ID::PluginRuntimeDiagnostic;
    using ID::PluginRuntimeState;

    int failures = 0;

    // ---- 1. 启动：产品按 profile 的编译期拓扑序执行 register 与 init ----
    const PluginRuntimeDiagnostic started = ID::Engine::start();
    check(failures, started.is_ok() && PluginRuntime::instance().state() == PluginRuntimeState::Ready,
        "installed product start state=Ready");
    if (!started.is_ok())
    {
        report_failure("start", started);
        return 1;
    }

#if defined(ID_CONSUMER_EXPECT_IDLOG)
    // ---- 2. ID.Log：服务表可用、修订一致、统计就绪、可写、可申请句柄 ----
    {
        const ID::Log::LogApiTable* const table =
            PluginRuntime::instance().get_service<ID::Log::LogApiTable>(ID::ServiceSlot::Log);

        check(failures, table != nullptr, "ID.Log service table published at ServiceSlot::Log");
        if (table != nullptr)
        {
            check(failures, table->revision == ID::Log::kLogApiRevision,
                "ID.Log service table revision matches installed public header");
            check(failures,
                table->write != nullptr && table->flush != nullptr && table->configure != nullptr
                    && table->reconfigure != nullptr && table->get_stats != nullptr
                    && table->acquire_logger != nullptr,
                "ID.Log service table entries are all non-null");
            check(failures,
                table->active_level != nullptr
                    && table->active_level->load(std::memory_order_relaxed)
                        <= static_cast<std::uint32_t>(ID::Log::Level::Fatal),
                "ID.Log active_level is readable and in range");

            ID::Log::LogStats stats{};
            table->get_stats(&stats);
            check(failures, stats.ready, "ID.Log runtime stats report ready=true");

            constexpr std::string_view category = "install-consumer";
            constexpr std::string_view message  = "installed product log probe";
            table->write(static_cast<std::uint32_t>(ID::Log::Level::Info),
                category.data(), static_cast<std::uint32_t>(category.size()),
                message.data(), static_cast<std::uint32_t>(message.size()));
            table->flush();

            check(failures,
                table->acquire_logger(category.data(), static_cast<std::uint32_t>(category.size())) != nullptr,
                "ID.Log acquire_logger returns a handle inside the Ready window");
        }
    }
#endif

#if defined(ID_CONSUMER_MATH_IS_SCALAR)
    // ---- 3. ID.Math.Scalar：只用本 backend 的公开头与命名空间 ----
    {
        // 修订常量必须与本产品安装的公开头一致（安装后仍需成立）
        static_assert(ID::Math::Scalar::kMathScalarApiRevision == 1,
            "ID.Math.Scalar 的接口修订是 1：与安装前缀中的 plugin.json 不一致时必须重编消费方");

        const ID::Math::Scalar::vec3 lhs(1.0f, 2.0f, 3.0f);
        const ID::Math::Scalar::vec3 rhs(4.0f, 5.0f, 6.0f);
        check(failures, ID::Math::Scalar::dot(lhs, rhs) == 32.0f,
            "ID.Math.Scalar dot(vec3, vec3) == 32");

        const ID::Math::Scalar::matrix<3, 3> unit = ID::Math::Scalar::identity<3>();
        check(failures,
            unit.get_element(0, 0) == 1.0f && unit.get_element(1, 1) == 1.0f
                && unit.get_element(0, 1) == 0.0f,
            "ID.Math.Scalar identity<3>() is the unit matrix");
    }
#elif defined(ID_CONSUMER_MATH_IS_SIMD)
    // ---- 3. ID.Math.SIMD：两个 backend 的 API 形态不同，这里按本 backend 的写法适配 ----
    {
        static_assert(ID::Math::SIMD::kMathSimdApiRevision == 1,
            "ID.Math.SIMD 的接口修订是 1：与安装前缀中的 plugin.json 不一致时必须重编消费方");

        const ID::Math::SIMD::vec3 lhs(1.0f, 2.0f, 3.0f);
        const ID::Math::SIMD::vec3 rhs(4.0f, 5.0f, 6.0f);
        check(failures, ID::Math::SIMD::dot(lhs, rhs) == 32.0f,
            "ID.Math.SIMD dot(vec3, vec3) == 32");

        const ID::Math::SIMD::matrix<3, 3> unit = ID::Math::SIMD::matrix<3, 3>::identity();
        check(failures,
            unit.get_element(0, 0) == 1.0f && unit.get_element(1, 1) == 1.0f
                && unit.get_element(0, 1) == 0.0f,
            "ID.Math.SIMD matrix<3,3>::identity() is the unit matrix");
    }
#endif

    // ---- 4. 关闭：第一次必须回到 Idle，第二次必须是幂等无操作 ----
    const PluginRuntimeDiagnostic stopped = ID::Engine::shutdown();
    check(failures, stopped.is_ok() && PluginRuntime::instance().state() == PluginRuntimeState::Idle,
        "installed product shutdown state=Idle");
    if (!stopped.is_ok())
    {
        report_failure("shutdown", stopped);
    }

    const PluginRuntimeDiagnostic stopped_again = ID::Engine::shutdown();
    check(failures,
        stopped_again.is_ok() && PluginRuntime::instance().state() == PluginRuntimeState::Idle,
        "installed product shutdown is idempotent (second call is a no-op in Idle)");
    if (!stopped_again.is_ok())
    {
        report_failure("shutdown-again", stopped_again);
    }

    std::printf("[%s] install-consumer summary: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
