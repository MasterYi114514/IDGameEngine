#pragma once

// 每个模块只包含自己的导出头；下游身份由 CMake 依据 JSON 解析结果以 PUBLIC 宏注入
#include "FixtureBetaExport.generated.hpp"

#include "FixtureModules.hpp"

/**
 *  FixtureBeta（fixture 模块 B：依赖上游 A 的插件）
 *  1. 本头不包含 A 的实现，只声明自己的入口；
 *  2. "我适配了 A"这件事由 ID_USE_PLUGIN_IDFIXTUREALPHA 身份宏 + 真实 include A 的公开能力头共同证明，
 *     缺宏时编译期直接报错（见 FixtureBetaModule.cpp）。
 */

namespace ID::Bench::Fixture
{
    inline constexpr char kBetaPluginName[] = "ID.Fixture.Beta";

    extern "C" ID_FIXTURE_BETA_API const ModuleState* FixtureBetaState() noexcept;
    extern "C" ID_FIXTURE_BETA_API const void* FixtureBetaKernelInstance() noexcept;

    /**
     *  @brief 在本模块内部做一次普通 new/delete
     *  @return 分配、写回与释放都成功返回 true
     *  @note 与 Alpha 的同名探针配合，证明每个模块各自接上了分配桩
     */
    extern "C" ID_FIXTURE_BETA_API bool FixtureBetaHeapProbe() noexcept;

    extern "C" ID_FIXTURE_BETA_API void FixtureBetaOnRegister() noexcept;
    extern "C" ID_FIXTURE_BETA_API bool FixtureBetaOnInit() noexcept;
    extern "C" ID_FIXTURE_BETA_API void FixtureBetaOnShutdown() noexcept;
    extern "C" ID_FIXTURE_BETA_API void FixtureBetaOnUnload() noexcept;
} // namespace ID::Bench::Fixture
