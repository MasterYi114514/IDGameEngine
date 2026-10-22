#pragma once

#include "IDWindowApi.hpp"

/**
 *  ID.Window 的生命周期声明（module + standard 的固定四阶段形状）
 *  1. 四个回调都必须 `noexcept`：运行期不允许用异常表达失败；
 *  2. `on_init` 返回 bool：false 表示初始化失败，runtime 会对"已尝试 init"的前缀（含本插件）
 *     逆序调用 `on_shutdown` 再逆序 `on_unload`；
 *  3. `ID_IDWINDOW_API` 由 `IDWindowApi.hpp` 链入的生成头 `IDWindowExport.generated.hpp` 提供，本头不手写；
 *  4. 本头只声明本插件自己的回调，不包含任何上游/下游生命周期声明。
 */

namespace ID::Window
{
    extern "C" ID_IDWINDOW_API void IDWindowOnRegister() noexcept;
    extern "C" ID_IDWINDOW_API bool IDWindowOnInit() noexcept;
    extern "C" ID_IDWINDOW_API void IDWindowOnShutdown() noexcept;
    extern "C" ID_IDWINDOW_API void IDWindowOnUnload() noexcept;
} // namespace ID::Window
