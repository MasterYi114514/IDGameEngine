#pragma once

#include <cstdint>

#include "Kernel/CoreTypes.hpp"

#include "detail/IDMathSIMDIsaGate.hpp"

#include "IDMathSIMDMatrix.hpp"
#include "IDMathSIMDVector.hpp"
#include "IDMathSIMDVectorOps.hpp"

/**
 *  ID.Math.SIMD 的唯一对外伞头（backend = SIMD）
 *  1. 本头聚合本 backend 的编译期常量与全部公开子头；下游只应包含它，不直接包含子头或 detail；
 *  2. `ID.Math.Scalar` 与 `ID.Math.SIMD` 是两个独立插件、两个 target、两个命名空间：同名类型
 *     互不相同、互不 `using`，两个伞头名字也互不相同（同时选中时 include 路径不会撞名）。
 *     两者不共享头文件、不互相包含；跨 backend 只允许显式逐元素转换，禁止 `reinterpret_cast`；
 *  3. `kMathSimdApiRevision` 必须与 `plugin.json` 的 `provides.api_revision` 一致；下游在依赖组的
 *     修订区间里用它做 `static_assert` 适配；
 *  4. 本 backend 不占 `ServiceSlot::Math`、没有运行期服务表、没有生命周期回调：能力全部是
 *     编译期契约与公开头里的内联实现；
 *  5. **基线 ISA = x64 SSE2**（D0-02）：本头第一件事就是包含 ISA 门，不满足基线时在预处理期
 *     `#error`，不存在运行期 ISA 调度、`cpuid` 探测或标量 fallback；AVX2/FMA 专用 variant 未冻结，
 *     本 backend 不为它预留开关；
 *  6. 本 backend 只在**消费方 TU** 里以编译期身份宏区分（`ID_USE_PLUGIN_IDMATHSIMD`，由组装层的
 *     PUBLIC usage requirement 注入，本头不自行定义、不检测"是否被选中"）。consumer 必须在包含前
 *     用 `#if defined(...)` 保护：同一份源码也要能在只选中 `ID.Math.Scalar` 的产品里编译。
 */

namespace ID::Math::SIMD
{
    /// 能力名（与 plugin.json 的 `name` / `provides[0].capability` 逐字一致）
    inline constexpr char kMathSimdApiName[] = "ID.Math.SIMD";

    /// 本实现的接口修订（不是插件发布版本），必须与 plugin.json 的 `provides.api_revision` 一致
    inline constexpr uint32_t kMathSimdApiRevision = 1;

    /// 编译期自证修订一致性：`plugin.json` 的 `provides.api_revision` 冻结为 1
    static_assert(kMathSimdApiRevision == 1, "ID.Math.SIMD 的接口修订是 1，改修订必须同步 plugin.json");
} // namespace ID::Math::SIMD
