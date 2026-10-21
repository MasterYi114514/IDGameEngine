#include "FixtureOptionalApi.hpp"
#include "IDFixtureNotSelectedLifecycle.hpp"

// 编译身份宏只由真实上游的 PUBLIC usage requirement 传播而来（计划书 8.3）。
// 这三条断言在 optional profile（Api + Core + Math + Optional 同时选中）里是**活**的：
// 全量注入身份宏会立刻在这里失败，而 fixture_math 里的反向 #error 同时验证相反方向。
#if !defined(ID_USE_PLUGIN_IDFIXTURENOTSELECTED)
    #error "IDFixtureNotSelected must receive ID_USE_PLUGIN_IDFIXTURENOTSELECTED from its own PUBLIC usage requirements"
#endif
// 本插件没有任何依赖：同一产品里的其它插件身份不得出现在本 TU
#if defined(ID_USE_PLUGIN_IDFIXTUREAPI) || defined(ID_USE_PLUGIN_IDFIXTURECORE) || defined(ID_USE_PLUGIN_IDFIXTUREMATH)
    #error "an unrelated plugin identity macro must not be injected into IDFixtureNotSelected: it declares no dependencies"
#endif

// 适配分支必须与 plugin.json 里声明的接口修订一致（计划书 4.4）：本插件没有依赖，
// 因此这里只能校验自己的 provides 修订与公开头一致，避免 JSON 与头文件两处漂移。
static_assert(ID::Fixture::Optional::kFixtureOptionalRevision == 1, "IDFixtureNotSelected provides ID.Fixture.Optional revision 1");

/**
 *  FixtureOptional 模块实现（可选 module 插件，提供无槽位能力）
 *  1. provides 里没有 slot：on_register 不发布函数表，runtime 也不会要求它发布；
 *  2. on_init 只做本模块的记账，不调用任何上游：它没有依赖；
 *  3. on_shutdown 归零，on_unload 无可注销内容。
 */

namespace ID::Fixture::Optional
{
    namespace
    {
        constinit uint32_t g_init_count = 0;    // on_init 成功次数（观测用，常量初始化）
    } // namespace

    extern "C" uint32_t FixtureOptionalInitCount() noexcept
    {
        return g_init_count;
    }

    extern "C" void IDFixtureNotSelectedOnRegister() noexcept
    {
        // 本插件没有服务槽位：on_register 不写任何槽（计划书 11.1 / 12.1）
    }

    extern "C" bool IDFixtureNotSelectedOnInit() noexcept
    {
        ++g_init_count;
        return true;
    }

    extern "C" void IDFixtureNotSelectedOnShutdown() noexcept
    {
        g_init_count = 0;
    }

    extern "C" void IDFixtureNotSelectedOnUnload() noexcept
    {
        // 本插件没有服务表可注销
    }
} // namespace ID::Fixture::Optional
