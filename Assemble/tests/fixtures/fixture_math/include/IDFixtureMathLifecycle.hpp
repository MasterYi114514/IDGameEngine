#pragma once

#include "FixtureMathApi.hpp"

/**
 *  module 插件的生命周期声明（本插件提供无槽位能力，on_register 不发布函数表）
 */

namespace ID::Fixture::Math
{
    extern "C" ID_IDFIXTUREMATH_API void IDFixtureMathOnRegister() noexcept;
    extern "C" ID_IDFIXTUREMATH_API bool IDFixtureMathOnInit() noexcept;
    extern "C" ID_IDFIXTUREMATH_API void IDFixtureMathOnShutdown() noexcept;
    extern "C" ID_IDFIXTUREMATH_API void IDFixtureMathOnUnload() noexcept;
} // namespace ID::Fixture::Math
