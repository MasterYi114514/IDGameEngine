#include "FixtureAlphaApi.hpp"

#include "Kernel/PluginRuntime.hpp"

/**
 *  FixtureAlpha 模块实现：提供一张能力表与四个生命周期回调
 *  1. 能力表是 constexpr 静态对象，不含运行期构造；
 *  2. 四个回调只写本模块状态，用于在验证程序里确认"回调确实在本模块内执行"；
 *  3. 这里刻意包含 Kernel/Plugin.hpp 并返回内核单例地址，用来证明 SHARED 下模块与宿主
 *     导入的是同一个 ID 内核实例。
 */

namespace ID::Bench::Fixture
{
    namespace
    {
        uint32_t alpha_mix(uint32_t value) noexcept
        {
            return value * 2654435761u + 17u;
        }

        constexpr AlphaApiTable kAlphaTable{ &alpha_mix, kAlphaPluginName };

        constinit ModuleState g_alpha_state{};     // 常量初始化，不存在运行期全局构造
    } // namespace

    extern "C" const AlphaApiTable* FixtureAlphaPublish() noexcept
    {
        return &kAlphaTable;
    }

    extern "C" const ModuleState* FixtureAlphaState() noexcept
    {
        return &g_alpha_state;
    }

    extern "C" const void* FixtureAlphaKernelInstance() noexcept
    {
        // 内核唯一实例：P0 由 PluginManager 运行期单例表达，P4 之后是 ID::PluginRuntime。
        // 只取地址，不触发生命周期，因此本原型自己的四阶段绑定表语义不变。
        return static_cast<const void*>(&ID::PluginRuntime::instance());
    }

    extern "C" bool FixtureAlphaHeapProbe() noexcept
    {
        int* block = new int(11);
        // volatile 读让分配地址真正逃逸，避免 -O3 把 new/delete 整对消除掉
        volatile int* sink = block;
        const bool    ok   = sink != nullptr && *sink == 11;
        delete block;
        return ok;
    }

    extern "C" void FixtureAlphaOnRegister() noexcept
    {
        ++g_alpha_state.on_register_count;
    }

    extern "C" bool FixtureAlphaOnInit() noexcept
    {
        ++g_alpha_state.on_init_count;
        return true;
    }

    extern "C" void FixtureAlphaOnShutdown() noexcept
    {
        ++g_alpha_state.on_shutdown_count;
    }

    extern "C" void FixtureAlphaOnUnload() noexcept
    {
        ++g_alpha_state.on_unload_count;
    }
} // namespace ID::Bench::Fixture
