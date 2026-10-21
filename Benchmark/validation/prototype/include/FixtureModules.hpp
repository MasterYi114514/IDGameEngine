#pragma once

#include <string_view>

#include "Kernel/CoreTypes.hpp"

/**
 *  P0 fixture 模块的公共描述（不是生产插件 ABI）
 *  1. ModuleLifecycle 用四个函数指针表达计划书 10.5 规定的四阶段回调形状；
 *  2. ModuleState 只用于在验证程序里观察"回调确实被调用过"，不参与任何产品逻辑；
 *  3. ModuleBinding 表在 EXE 侧静态初始化，名字来自静态存储，函数指针指向真实模块符号。
 */

namespace ID::Bench::Fixture
{
    // 四个生命周期回调，签名与计划书 10.5 一致：全部 noexcept
    struct ModuleLifecycle
    {
        void (*on_register)() noexcept = nullptr;
        bool (*on_init)() noexcept     = nullptr;
        void (*on_shutdown)() noexcept = nullptr;
        void (*on_unload)() noexcept   = nullptr;
    };

    // 模块内部状态（由模块自己拥有，导出只读指针）
    struct ModuleState
    {
        uint32_t on_register_count    = 0;      // on_register 被调用次数
        uint32_t on_init_count        = 0;      // on_init 被调用次数
        uint32_t on_shutdown_count    = 0;      // on_shutdown 被调用次数
        uint32_t on_unload_count      = 0;      // on_unload 被调用次数
        uint32_t last_upstream_value  = 0;      // 最近一次通过上游公开 API 得到的计算结果
    };

    // 运行期绑定表的一行：插件名 + 四个回调地址
    struct ModuleBinding
    {
        std::string_view name;          // 期望插件名（与编译期身份逐项核验）
        ModuleLifecycle  lifecycle;     // 真实模块符号地址
    };

    // 产品入口：由装配静态库实现，参考实现只做四阶段调用，不含运行期状态机（P3 才实现 PluginRuntime）
    extern "C" bool FixtureProductStart() noexcept;
    extern "C" void FixtureProductStop() noexcept;
} // namespace ID::Bench::Fixture
