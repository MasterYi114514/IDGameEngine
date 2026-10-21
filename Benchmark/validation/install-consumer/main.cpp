#include <cstdio>
#include <string_view>

#include "IDGameEngine.hpp"

/**
 *  P5 安装消费校验程序
 *  1. 只 include 安装前缀里的公开头 `IDGameEngine.hpp`，不碰任何源码树路径；
 *  2. 显式调用 `ID::Engine::start()` 并检查结构化诊断，再 `shutdown()` 回到 Idle；
 *  3. 输出只走 stdout，不写 report.txt，也不链接 ID::Bench 支持库。
 *  @note 这不是 Benchmark 测试域；它验证的是"安装后在另一个构建目录消费成功"（计划书 13.4 / 16.3）。
 */

namespace
{
    std::string_view error_text(ID::PluginRuntimeError code) noexcept
    {
        return ID::plugin_runtime_error_name(code);
    }

    std::string_view state_text(ID::PluginRuntimeState state) noexcept
    {
        return ID::plugin_runtime_state_name(state);
    }
} // namespace

int main()
{
    const ID::PluginRuntimeDiagnostic started = ID::Engine::start();
    if (!started.is_ok())
    {
        std::printf("[FAIL] start code=%.*s state=%.*s\n",
            static_cast<int>(error_text(started.code).size()), error_text(started.code).data(),
            static_cast<int>(state_text(started.state).size()), state_text(started.state).data());
        return 1;
    }

    const bool ready = ID::PluginRuntime::instance().state() == ID::PluginRuntimeState::Ready;

    const ID::PluginRuntimeDiagnostic stopped = ID::Engine::shutdown();
    const bool idle = stopped.is_ok() && ID::PluginRuntime::instance().state() == ID::PluginRuntimeState::Idle;

    std::printf("[%s] installed product start state=%.*s\n",
        ready ? "PASS" : "FAIL",
        static_cast<int>(state_text(started.state).size()), state_text(started.state).data());
    std::printf("[%s] installed product shutdown state=%.*s\n",
        idle ? "PASS" : "FAIL",
        static_cast<int>(state_text(stopped.state).size()), state_text(stopped.state).data());

    return (ready && idle) ? 0 : 1;
}
