#pragma once

#include <cstdint>

#include "Common/Core.hpp"
#include "Kernel/CoreTypes.hpp"

// 逐插件导出头由组装生成器产出：STATIC 归并时宏为空，Windows SHARED 时只有本插件自己的
// build 宏（PRIVATE，不向下游传播）能打开 dllexport（计划书 12.1）
#include "IDFixtureNotSelectedExport.generated.hpp"

/**
 *  集成用最小 fixture（可选 module 插件）：不是业务插件，只用于验证 P2 组装流水线
 *  1. 能力独立（ID.Fixture.Optional）、**没有运行期服务槽位**：on_register 不发布任何函数表；
 *  2. 目标名刻意取 IDFixtureNotSelected：它对应的身份宏 ID_USE_PLUGIN_IDFIXTURENOTSELECTED
 *     就是 fixture_math 里“无关身份宏不得注入”那条 #error 检查的名字，因此默认 profile
 *     （不选中本插件）与 optional profile（选中本插件）分别构成该检查的“无宏/有宏”两条真实路径；
 *  3. 本插件没有任何依赖，所以它不得收到同一产品里其它插件的身份宏，由实现 TU 的 #error 断言。
 */

namespace ID::Fixture::Optional
{
    inline constexpr char     kFixtureOptionalName[]   = "ID.Fixture.Optional";
    inline constexpr uint32_t kFixtureOptionalRevision = 1;    // 能力接口修订，必须与 plugin.json 一致

    /**
     *  @brief 观测本模块 on_init 的调用次数
     *  @return 已经完成初始化的次数（on_shutdown 归零）
     */
    extern "C" ID_IDFIXTURENOTSELECTED_API uint32_t FixtureOptionalInitCount() noexcept;
} // namespace ID::Fixture::Optional
