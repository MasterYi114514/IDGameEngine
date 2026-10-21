#include "IDFixtureCoreLifecycle.hpp"

#include "Kernel/PluginRuntime.hpp"

/**
 *  FixtureCore 模块实现
 *  1. on_register 只做本插件槽位的服务发布：越权、重复发布、非 Registering 阶段发布
 *     都由 runtime 在冷路径拒绝并记账；
 *  2. on_init 在本模块内部做一次普通 new/delete：用来证明“本模块自己的分配”也走本模块
 *     链接的分配桩，而不是只证明宿主能分配；
 *  3. on_unload 注销自己的槽：不清别人的服务。
 */

namespace ID::Fixture::Core
{
    namespace
    {
        uint32_t core_mix(uint32_t value) noexcept
        {
            return value * 2654435761u + 7u;
        }

        constexpr FixtureCoreApiTable kCoreTable{ &core_mix, kFixtureCoreName };

        constinit uint32_t g_init_count = 0;      // on_init 成功次数（观测用，常量初始化）
        constinit bool     g_heap_route = false;  // 本模块的 new/delete 是否真的可用
    } // namespace

    extern "C" const FixtureCoreApiTable* FixtureCorePublish() noexcept
    {
        return &kCoreTable;
    }

    extern "C" uint32_t FixtureCoreInitCount() noexcept
    {
        return g_init_count;
    }

    extern "C" void IDFixtureCoreOnRegister() noexcept
    {
        ID::PluginRuntime::instance().publish_service(kFixtureCoreSlot, &kCoreTable);
    }

    extern "C" bool IDFixtureCoreOnInit() noexcept
    {
        int* const block = new int(3);
        if (block == nullptr)
        {
            return false;
        }
        g_heap_route = (*block == 3);

        // volatile 写让分配地址真正逃逸，避免优化器把 new/delete 整对消除
        volatile int* sink = block;
        *sink = 4;

        delete block;
        ++g_init_count;
        return g_heap_route;
    }

    extern "C" void IDFixtureCoreOnShutdown() noexcept
    {
        g_heap_route = false;
    }

    extern "C" void IDFixtureCoreOnUnload() noexcept
    {
        ID::PluginRuntime& runtime = ID::PluginRuntime::instance();
        const FixtureCoreApiTable* const table =
            runtime.get_service<FixtureCoreApiTable>(static_cast<ID::ServiceSlot>(kFixtureCoreSlot));
        if (table != nullptr)
        {
            runtime.revoke_service(kFixtureCoreSlot);
        }
    }
} // namespace ID::Fixture::Core
