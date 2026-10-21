#pragma once

#include <cstdint>

#include "Common/Core.hpp"
#include "Kernel/CoreTypes.hpp"

#include "IDFixtureMathExport.generated.hpp"

/**
 *  集成用最小 fixture（下游 module 插件）：不是业务插件，只用于验证 P2 组装流水线
 *  1. 它提供的能力 `ID.Fixture.Math` **没有服务槽位**（provides 里不写 slot）：
 *     用来验证“无槽位能力”在编译期计划与运行期预检里都合法；
 *  2. 本插件适配过的上游只通过各自的公开能力头使用，不包含上游实现。
 */

namespace ID::Fixture::Math
{
    inline constexpr char     kFixtureMathName[]   = "ID.Fixture.Math";
    inline constexpr uint32_t kFixtureMathRevision = 1;    // 本实现的能力接口修订

    /**
     *  @brief 观测下游调用上游能力后的计算结果
     *  @return on_init 里调用上游能力得到的值；未初始化时返回 0
     */
    extern "C" ID_IDFIXTUREMATH_API uint32_t FixtureMathResult() noexcept;
} // namespace ID::Fixture::Math
