// 上游公开能力头：只能通过“真实上游 target 的 PUBLIC include”拿到，不存在全局 include 注入
#include "FixtureCoreApi.hpp"
#include "FixtureApiCore.hpp"

#include "IDFixtureMathLifecycle.hpp"

#include "Kernel/PluginRuntime.hpp"

// 编译身份宏只由真实上游的 PUBLIC usage requirement 传播而来：
// 缺少它说明组装器漏接了一条真实链接边，直接在编译期报错，绝不静默降级（计划书 8.3）
#if !defined(ID_USE_PLUGIN_IDFIXTURECORE)
    #error "IDFixtureMath must receive ID_USE_PLUGIN_IDFIXTURECORE from the IDFixtureCore PUBLIC usage requirements"
#endif
#if !defined(ID_USE_PLUGIN_IDFIXTUREAPI)
    #error "IDFixtureMath must receive ID_USE_PLUGIN_IDFIXTUREAPI from the IDFixtureApi PUBLIC usage requirements"
#endif
// 无关系插件不得被注入到本插件：全量注入会掩盖漏声明依赖
#if defined(ID_USE_PLUGIN_IDFIXTUREMATH) && defined(ID_USE_PLUGIN_IDFIXTURENOTSELECTED)
    #error "unrelated plugin identity macro must not be injected into IDFixtureMath"
#endif

// 适配分支必须与 plugin.json 里声明的接口修订区间一致（计划书 4.4）：
// 只写进 JSON 而没有真实的编译期断言，不算适配
static_assert(ID::Fixture::Core::kFixtureCoreRevision == 2, "IDFixtureMath is adapted to ID.Fixture.Core revision 2");
static_assert(ID::Fixture::Api::kFixtureApiRevision == 1, "IDFixtureMath is adapted to ID.Fixture.Api revision 1");

/**
 *  FixtureMath 模块实现（下游 module 插件，提供无槽位能力）
 *  1. provides 里没有 slot：on_register 不发布函数表，runtime 也不会要求它发布；
 *  2. on_init 需要两个上游：一个有运行期函数表（Core，槽位 60 由 on_register 发布），
 *     一个是纯头文件能力（Api，编译期内联）；
 *  3. 两个上游都真实调用一次，证明“构建期链接关系 / 编译期身份宏 / 运行期服务寻址”三处一致。
 */

namespace ID::Fixture::Math
{
    namespace
    {
        constinit uint32_t g_result = 0;    // on_init 里上游调用的结果（观测用，常量初始化）

        // 上游 Core 的服务槽位：来自 FixtureCoreApi.hpp 的公开常量，不在这里另写一份
        inline constexpr uint32_t kUpstreamCoreSlot = ID::Fixture::Core::kFixtureCoreSlot;
    } // namespace

    extern "C" uint32_t FixtureMathResult() noexcept
    {
        return g_result;
    }

    extern "C" void IDFixtureMathOnRegister() noexcept
    {
        // 本插件没有服务槽位：on_register 不写任何槽（计划书 11.1 / 12.1）
    }

    extern "C" bool IDFixtureMathOnInit() noexcept
    {
        const ID::Fixture::Core::FixtureCoreApiTable* const core =
            ID::PluginRuntime::instance().get_service<ID::Fixture::Core::FixtureCoreApiTable>(
                static_cast<ID::ServiceSlot>(kUpstreamCoreSlot));
        if (core == nullptr || core->mix == nullptr)
        {
            return false;
        }

        // 运行期函数表与上游内联能力各调用一次
        g_result = core->mix(ID::Fixture::Api::fixture_api_scale(7u));
        return g_result != 0;
    }

    extern "C" void IDFixtureMathOnShutdown() noexcept
    {
        g_result = 0;
    }

    extern "C" void IDFixtureMathOnUnload() noexcept
    {
        // 本插件没有服务表可注销
    }
} // namespace ID::Fixture::Math
