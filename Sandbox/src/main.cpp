#include <cstdio>
#include <string_view>

#include "IDGameEngine.hpp"

/**
 *  Sandbox：游戏本体宿主（计划书 9.3）
 *  1. 唯一启动方式是通过产品入口 `ID::Engine::start()`：本层不登记插件描述符、不手工安装插件、
 *     也没有静态构造自动启动；插件集合完全由构建期的产品 profile 决定；
 *  2. start/shutdown 的返回值必须检查：失败时 runtime 已经回滚到 Idle，宿主打印结构化诊断后退出；
 *  3. 本文件不承担任何正确性或性能验证职责，那些全部归 Benchmark；
 *  4. 启动期日志配置当前没有可用注入点：`ID::Engine::start()` 在一次调用内先跑完全部 `on_register`
 *     再跑完全部 `on_init`，`ID.Log` 在自身 `on_init` 用默认配置建后端，而 `configure`/`reconfigure`
 *     在那就绪之前返回 NotReady。因此本文件不配置日志，也**不得**用 start 之后 reconfigure
 *     冒充启动期配置（那会丢掉下游在 `on_init` 写的启动期文件日志）；注入点落地后再接线。
 */

namespace
{
    /**
     *  @brief 把结构化诊断打印成一行（冷路径：只在启动/关闭失败时调用）
     *  @param stage 失败发生的动作名
     *  @param diagnostic runtime 返回的诊断
     */
    void report_failure(std::string_view stage, const ID::PluginRuntimeDiagnostic& diagnostic) noexcept
    {
        const std::string_view code  = ID::plugin_runtime_error_name(diagnostic.code);
        const std::string_view state = ID::plugin_runtime_state_name(diagnostic.state);
        std::printf("[Sandbox] %.*s failed: code=%.*s state=%.*s slot=%d\n",
            static_cast<int>(stage.size()), stage.data(),
            static_cast<int>(code.size()), code.data(),
            static_cast<int>(state.size()), state.data(),
            static_cast<int>(diagnostic.slot));
    }
} // namespace

int main()
{
    const ID::PluginRuntimeDiagnostic started = ID::Engine::start();
    if (!started.is_ok())
    {
        report_failure("start", started);
        return 1;
    }

    std::printf("[Sandbox] product started (state=Ready)\n");

    // 游戏逻辑从这里开始。本宿主只经 ID::Engine 消费产品：产品按 profile 选中的插件
    // （集成 fixture 或业务插件 IDLog）的编译期拓扑序启动。

    const ID::PluginRuntimeDiagnostic stopped = ID::Engine::shutdown();
    if (!stopped.is_ok())
    {
        report_failure("shutdown", stopped);
        return 1;
    }

    std::printf("[Sandbox] product stopped (state=Idle)\n");
    return 0;
}
