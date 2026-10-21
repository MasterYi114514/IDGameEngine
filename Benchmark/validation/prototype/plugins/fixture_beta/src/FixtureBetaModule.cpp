#include "FixtureBetaApi.hpp"

/**
 *  FixtureBeta 模块实现：依赖上游 Alpha 的下游插件
 *  1. 身份宏由组装配置以 PUBLIC 方式注入，缺宏直接 #error —— 这是"三处一致"里的编译期身份一环；
 *  2. 上游插件的 PRIVATE build 宏不得传播到本模块，否则导出归属会出错；
 *  3. 适配代码真实存在：包含上游公开能力头、校验接口修订、在 on_init 里真正调用上游函数指针。
 */

#if !defined(ID_USE_PLUGIN_IDFIXTUREALPHA)
    #error "ID.Fixture.Beta: 缺少上游身份宏 ID_USE_PLUGIN_IDFIXTUREALPHA（组装配置未接线）"
#endif

#if defined(ID_FIXTURE_ALPHA_BUILD_DLL)
    #error "ID.Fixture.Beta: 上游插件的 PRIVATE build 宏不得向下游传播"
#endif

#include "FixtureAlphaApi.hpp"
#include "Kernel/PluginRuntime.hpp"

// 公开头声明的接口修订必须与下游适配分支一致（计划书 4.4）
static_assert(ID::Bench::Fixture::kAlphaApiRevision == 1,
              "ID_FixtureBeta: 只适配 ID.Fixture.Alpha 的 api_revision 1");

namespace ID::Bench::Fixture
{
    namespace
    {
        constinit ModuleState g_beta_state{};      // 常量初始化，不存在运行期全局构造
    } // namespace

    extern "C" const ModuleState* FixtureBetaState() noexcept
    {
        return &g_beta_state;
    }

    extern "C" const void* FixtureBetaKernelInstance() noexcept
    {
        // 与 Alpha 同一语义：内核唯一实例在 P4 之后是 ID::PluginRuntime，只取地址
        return static_cast<const void*>(&ID::PluginRuntime::instance());
    }

    extern "C" bool FixtureBetaHeapProbe() noexcept
    {
        int* block = new int(13);
        volatile int* sink = block;
        const bool    ok   = sink != nullptr && *sink == 13;
        delete block;
        return ok;
    }

    extern "C" void FixtureBetaOnRegister() noexcept
    {
        ++g_beta_state.on_register_count;
    }

    extern "C" bool FixtureBetaOnInit() noexcept
    {
        ++g_beta_state.on_init_count;

        // 真实使用已选上游的公开 API：跨模块间接调用，结果写回本模块状态
        const AlphaApiTable* upstream = FixtureAlphaPublish();
        if (upstream == nullptr || upstream->mix == nullptr || upstream->name == nullptr)
        {
            return false;
        }
        g_beta_state.last_upstream_value = upstream->mix(21u);
        return true;
    }

    extern "C" void FixtureBetaOnShutdown() noexcept
    {
        ++g_beta_state.on_shutdown_count;
    }

    extern "C" void FixtureBetaOnUnload() noexcept
    {
        ++g_beta_state.on_unload_count;
    }
} // namespace ID::Bench::Fixture
