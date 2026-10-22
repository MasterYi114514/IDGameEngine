#pragma once

#include <cstdint>

#include "Kernel/CoreTypes.hpp"

#include "IDMathScalarMatrix.hpp"
#include "IDMathScalarMatrixOps.hpp"
#include "IDMathScalarVector.hpp"
#include "IDMathScalarVectorOps.hpp"

/**
 *  ID.Math.Scalar 的唯一对外伞头（backend = Scalar）
 *  1. 本头聚合本 backend 的编译期常量与全部公开子头；下游只应包含它，不直接包含子头或 detail；
 *  2. `ID.Math.Scalar` 与 `ID.Math.SIMD` 是两个独立插件、两个 target、两个命名空间：同名类型
 *     互不相同、互不 `using`，两个伞头名字也互不相同（同时选中时 include 路径不会撞名）；
 *  3. `kMathScalarApiRevision` 必须与 `plugin.json` 的 `provides.api_revision` 一致；下游在依赖组的
 *     修订区间里用它做 `static_assert` 适配；
 *  4. 本 backend 不占 `ServiceSlot::Math`、没有运行期服务表、没有生命周期回调：能力全部是
 *     编译期契约与公开头里的内联实现。
 */

namespace ID::Math::Scalar
{
    /// 能力名（与 plugin.json 的 `name` / `provides[0].capability` 逐字一致）
    inline constexpr char kMathScalarApiName[] = "ID.Math.Scalar";

    /// 本实现的接口修订（不是插件发布版本），必须与 plugin.json 的 `provides.api_revision` 一致
    inline constexpr uint32_t kMathScalarApiRevision = 1;
} // namespace ID::Math::Scalar
