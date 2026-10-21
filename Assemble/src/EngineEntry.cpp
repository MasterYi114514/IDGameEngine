#include "IDGameEngine.hpp"

#include "IDAssemblyRuntime.generated.hpp"

/**
 *  产品入口实现（计划书 9.2 / 9.3）
 *  1. 本 TU 位于 `IDAssembly` 静态库：宿主调用入口时该对象才会被从归档提取，
 *     从而把绑定 TU 与选中插件的真实回调一起拉进最终程序；
 *  2. 计划视图来自生成物，只做“把静态计划交给运行时”这一件事；
 *  3. 不在这里初始化内核内存或日志：那是宿主/内核自身的职责，入口只负责插件生命周期。
 */

namespace ID::Engine
{
    PluginRuntimeDiagnostic start() noexcept
    {
        return PluginRuntime::instance().start(*Assembly::Generated::ID_AssemblyPlanView());
    }

    PluginRuntimeDiagnostic shutdown() noexcept
    {
        return PluginRuntime::instance().shutdown();
    }
} // namespace ID::Engine
