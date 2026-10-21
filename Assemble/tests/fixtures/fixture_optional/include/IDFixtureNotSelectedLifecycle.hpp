#pragma once

#include "FixtureOptionalApi.hpp"

/**
 *  module 插件的生命周期声明（本插件提供无槽位能力，on_register 不发布函数表）
 *  1. 四个回调都必须 noexcept：运行期不允许用异常表达失败；
 *  2. 本头只声明本插件自己的回调，不包含任何上游/下游生命周期声明。
 */

namespace ID::Fixture::Optional
{
    extern "C" ID_IDFIXTURENOTSELECTED_API void IDFixtureNotSelectedOnRegister() noexcept;
    extern "C" ID_IDFIXTURENOTSELECTED_API bool IDFixtureNotSelectedOnInit() noexcept;
    extern "C" ID_IDFIXTURENOTSELECTED_API void IDFixtureNotSelectedOnShutdown() noexcept;
    extern "C" ID_IDFIXTURENOTSELECTED_API void IDFixtureNotSelectedOnUnload() noexcept;
} // namespace ID::Fixture::Optional
