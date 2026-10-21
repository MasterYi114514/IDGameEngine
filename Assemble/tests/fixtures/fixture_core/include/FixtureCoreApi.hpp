#pragma once

#include <cstdint>

#include "Common/Core.hpp"
#include "Kernel/CoreTypes.hpp"

// 逐插件导出头由组装生成器产出：STATIC 归并时宏为空，Windows SHARED 时只有本插件自己的
// build 宏（PRIVATE，不向下游传播）能打开 dllexport（计划书 12.1）
#include "IDFixtureCoreExport.generated.hpp"

/**
 *  集成用最小 fixture（module 插件）：不是业务插件，只用于验证 P2 组装流水线
 *  1. 公开的是一张能力表 + 四个生命周期回调，不暴露内部实现；
 *  2. 导出入口全部 extern "C"，符号名可被 nm/objdump 直接核对；
 *  3. 能力带服务槽位 60（fixture 保留槽位，真实业务槽位见 ServiceSlot），
 *     因此本插件的 on_register 必须在计划分配的槽上发布函数表。
 */

namespace ID::Fixture::Core
{
    inline constexpr char     kFixtureCoreName[]     = "ID.Fixture.Core";
    inline constexpr uint32_t kFixtureCoreRevision   = 2;     // 能力接口修订，必须与 plugin.json 一致
    inline constexpr uint32_t kFixtureCoreSlot       = 60;    // fixture 保留槽位（0..63）

    // 上游能力表：固定函数指针 + 名字，纯 POD，跨模块只传指针
    struct FixtureCoreApiTable
    {
        uint32_t (*mix)(uint32_t) noexcept = nullptr;    // 一次确定性混合计算
        const char* name                   = nullptr;    // 能力名（静态存储）
    };

    /**
     *  @brief 取得本模块的能力表（静态存储，进程生命周期内有效）
     *  @return 能力表指针，永不为空
     */
    extern "C" ID_IDFIXTURECORE_API const FixtureCoreApiTable* FixtureCorePublish() noexcept;

    /**
     *  @brief 观测本模块 on_init 的调用次数
     *  @return 已经完成初始化的次数
     */
    extern "C" ID_IDFIXTURECORE_API uint32_t FixtureCoreInitCount() noexcept;
} // namespace ID::Fixture::Core
