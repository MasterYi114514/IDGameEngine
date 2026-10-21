#pragma once

#include "Kernel/PluginRuntime.hpp"

/**
 *  IDGameEngine 产品消费入口（Assemble 层）
 *  1. `ID::Engine::start()` / `ID::Engine::shutdown()` 是本产品的一次性运行期入口：
 *     内部把组装层生成的静态计划视图交给内核运行期唯一实例，不重新求解依赖、不重排拓扑；
 *  2. 它们是组装层包装，**不是** `PluginManager` 的第四个接口，也不属于内核；
 *  3. 应用必须显式调用 start 并检查返回值；本层不提供静态构造自动启动，
 *     也不承诺链接器会替调用方启动引擎（计划书 9.3）；
 *  4. 本头不泄露任何插件的内部实现：只暴露内核公开类型与两个入口函数声明，
 *     具体插件能力头由消费方按实际启用集合自行包含（身份宏经 target 的 PUBLIC usage requirement 传播）。
 */

namespace ID::Engine
{
    /**
     *  @brief 启动本产品的全部选中插件（按编译期拓扑序执行 register 与 init）
     *  @return 结构化诊断。成功时 code 为 None、state 为 Ready；
     *          失败时 runtime 已回滚到 Idle 并返回带阶段/插件/槽位的错误，宿主可修正条件后重试
     *  @note 只在宿主控制线程调用；Ready 时重复调用返回 AlreadyStarted，回调期间重入返回 Busy
     */
    PluginRuntimeDiagnostic start() noexcept;

    /**
     *  @brief 关闭本产品（逆拓扑序执行 shutdown 与 unload，并清空服务槽）
     *  @return 结构化诊断。成功时 code 为 None、state 为 Idle；Idle 调用是幂等无操作
     *  @note 宿主必须在调用前停止外部业务访问；插件自身任务由各插件 on_shutdown 收敛
     */
    PluginRuntimeDiagnostic shutdown() noexcept;
} // namespace ID::Engine
