#pragma once

#include "FixtureCoreApi.hpp"

/**
 *  module 插件的生命周期声明（计划书 10.5 的固定四阶段形状）
 *  1. 四个回调都必须 noexcept：运行期不允许用异常表达失败；
 *  2. on_init 返回 bool：false 表示初始化失败，runtime 会对“已尝试 init”的前缀（含本插件）
 *     逆序调用 on_shutdown 再逆序 on_unload；
 *  3. 本头只声明本插件自己的回调，不包含任何上游/下游生命周期声明。
 */

namespace ID::Fixture::Core
{
    extern "C" ID_IDFIXTURECORE_API void IDFixtureCoreOnRegister() noexcept;
    extern "C" ID_IDFIXTURECORE_API bool IDFixtureCoreOnInit() noexcept;
    extern "C" ID_IDFIXTURECORE_API void IDFixtureCoreOnShutdown() noexcept;
    extern "C" ID_IDFIXTURECORE_API void IDFixtureCoreOnUnload() noexcept;
} // namespace ID::Fixture::Core
