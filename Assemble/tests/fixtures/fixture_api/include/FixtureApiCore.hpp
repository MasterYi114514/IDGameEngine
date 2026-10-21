#pragma once

#include <cstdint>

#include "Common/Core.hpp"
#include "Kernel/CoreTypes.hpp"

/**
 *  集成用最小 fixture（interface 插件，纯头文件能力）：不是业务插件，只用于验证 P2 组装流水线
 *  1. kind=interface 首版规定 lifecycle=none、没有运行期服务槽：只交付公开头与编译身份；
 *  2. 本头不包含任何上游/下游插件头，符合“上游只交付能力、不交付实现”；
 *  3. `kFixtureApiRevision` 是能力接口修订，必须与 plugin.json 的 provides.api_revision 一致，
 *     下游用它做适配分支的 static_assert（计划书 4.4）。
 */

namespace ID::Fixture::Api
{
    inline constexpr char     kFixtureApiName[]     = "ID.Fixture.Api";
    inline constexpr uint32_t kFixtureApiRevision   = 1;    // 能力接口修订（不是插件发布版本）
    inline constexpr uint32_t kFixtureApiGrade      = 3;    // 一次纯头文件变换的系数，供下游内联调用

    /**
     *  @brief 纯头文件的确定性变换（编译期内联，没有函数表也没有跨模块调用）
     *  @param value 输入值
     *  @return value * 3 + 1
     */
    ID_FORCEINLINE constexpr uint32_t fixture_api_scale(uint32_t value) noexcept
    {
        return value * kFixtureApiGrade + 1u;
    }
} // namespace ID::Fixture::Api
