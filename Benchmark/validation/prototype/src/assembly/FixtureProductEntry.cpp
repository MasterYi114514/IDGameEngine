#include <array>
#include <cstddef>

#include "FixtureAlphaApi.hpp"
#include "FixtureBetaApi.hpp"
#include "FixtureModules.hpp"

/**
 *  产品入口（P0 的最小替身，不是 PluginRuntime）
 *  1. 绑定表是 constexpr 数组：函数地址属于链接期常量，不存在运行期全局构造（计划书 11.3）；
 *  2. 表中只包含选中插件的真实回调地址，未选中插件在链接中根本不存在；
 *  3. 该 TU 位于 IDFixtureAssembly 静态库，宿主 main 只调用 FixtureProductStart/Stop，
 *     因此"归档里的入口对象被提取 + 回调被真实引用"这两件事可以同时验证（计划书 9.2）。
 */

namespace ID::Bench::Fixture
{
    namespace
    {
        constexpr std::array<ModuleBinding, 2> kBinds{ {
            { kAlphaPluginName,
              ModuleLifecycle{ &FixtureAlphaOnRegister, &FixtureAlphaOnInit,
                               &FixtureAlphaOnShutdown, &FixtureAlphaOnUnload } },
            { kBetaPluginName,
              ModuleLifecycle{ &FixtureBetaOnRegister, &FixtureBetaOnInit,
                               &FixtureBetaOnShutdown, &FixtureBetaOnUnload } },
        } };

        // 绑定表的插件名必须与各模块公开头里的编译期身份逐字一致
        static_assert(kBinds[0].name == kAlphaPluginName, "Alpha 绑定项名字必须与模块身份一致");
        static_assert(kBinds[1].name == kBetaPluginName, "Beta 绑定项名字必须与模块身份一致");
    } // namespace

    extern "C" bool FixtureProductStart() noexcept
    {
        for (const ModuleBinding& binding : kBinds)
        {
            binding.lifecycle.on_register();
        }
        for (const ModuleBinding& binding : kBinds)
        {
            if (!binding.lifecycle.on_init())
            {
                return false;
            }
        }
        return true;
    }

    extern "C" void FixtureProductStop() noexcept
    {
        for (std::size_t i = kBinds.size(); i > 0; --i)
        {
            kBinds[i - 1].lifecycle.on_shutdown();
        }
        for (std::size_t i = kBinds.size(); i > 0; --i)
        {
            kBinds[i - 1].lifecycle.on_unload();
        }
    }
} // namespace ID::Bench::Fixture
