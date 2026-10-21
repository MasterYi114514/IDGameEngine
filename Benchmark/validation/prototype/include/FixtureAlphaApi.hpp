#pragma once

// 逐插件导出头由 CMake 从 templates/PluginExport.generated.hpp.in 生成：
// STATIC 归并时宏为空；Windows SHARED 时只有本插件自己的 build 宏决定 dllexport/dllimport
#include "FixtureAlphaExport.generated.hpp"

#include "FixtureModules.hpp"

/**
 *  FixtureAlpha（fixture 模块 A：提供上游能力的插件）
 *  1. 公开的是能力表 + 生命周期回调，不暴露内部实现；
 *  2. 导出入口全部 extern "C"，符号名可被 nm/objdump 直接核对，便于验证真实跨模块引用；
 *  3. 本头不包含任何下游插件头，符合"上游只交付能力、不交付实现"。
 */

namespace ID::Bench::Fixture
{
    inline constexpr char     kAlphaPluginName[]  = "ID.Fixture.Alpha";
    inline constexpr uint32_t kAlphaApiRevision   = 1;      // 公开能力表修订号，下游用 static_assert 校验

    // 上游能力表：固定函数指针 + 名字，属于纯 POD，跨 DLL 只传指针
    struct AlphaApiTable
    {
        uint32_t (*mix)(uint32_t) noexcept = nullptr;    // 一次确定性混合计算
        const char* name                   = nullptr;    // 能力名（静态存储）
    };

    /**
     *  @brief 取得本插件的能力表
     *  @return 指向模块内部静态表的指针，进程生命周期内有效
     */
    extern "C" ID_FIXTURE_ALPHA_API const AlphaApiTable* FixtureAlphaPublish() noexcept;

    /**
     *  @brief 取得本模块的生命周期状态（只读）
     *  @return 模块内部状态指针，用于验证回调确实在本模块内执行
     */
    extern "C" ID_FIXTURE_ALPHA_API const ModuleState* FixtureAlphaState() noexcept;

    /**
     *  @brief 取得本模块看到的 ID 内核唯一实例地址
     *  @return &ID::PluginRuntime::instance()（P4 退役 PluginManager 运行期单例后的内核唯一实例）；
     *          用于验证 SHARED 下模块与宿主看到同一个内核实例
     */
    extern "C" ID_FIXTURE_ALPHA_API const void* FixtureAlphaKernelInstance() noexcept;

    /**
     *  @brief 在本模块内部做一次普通 new/delete
     *  @return 分配、写回与释放都成功返回 true
     *  @note 用来证明"本模块自己的 new/delete 走本模块链接的分配桩"，
     *        不只证明宿主可分配；没有任何分配引用的模块会被链接器丢掉桩对象。
     */
    extern "C" ID_FIXTURE_ALPHA_API bool FixtureAlphaHeapProbe() noexcept;

    extern "C" ID_FIXTURE_ALPHA_API void FixtureAlphaOnRegister() noexcept;
    extern "C" ID_FIXTURE_ALPHA_API bool FixtureAlphaOnInit() noexcept;
    extern "C" ID_FIXTURE_ALPHA_API void FixtureAlphaOnShutdown() noexcept;
    extern "C" ID_FIXTURE_ALPHA_API void FixtureAlphaOnUnload() noexcept;
} // namespace ID::Bench::Fixture
