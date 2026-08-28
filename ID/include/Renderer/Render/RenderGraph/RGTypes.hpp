#pragma once

#include "IDpch.hpp"

namespace ID
{
    /*
    *   RGResource：语义资源槽位，与 RenderContext 中的字段一一对应。
    *   Phase 4a（本期）：把 RenderContext 的公共字段显式化 + 版本化，即可得到真 DAG；
    *   Phase 4b 将扩展为任意命名瞬态资源，此枚举届时退役。
    */
    enum class RGResource : uint8_t
    {
        ShadowMap,       // ctx.shadow_fb（由 ShadowPass 写入）
        SceneColor,      // ctx.scene_fb（Forward/Skybox/Transparent 读改写）
        ViewportTarget,  // ctx.viewport_fb（PostProcess 写入，最终呈现目标）

        Count
    };

    /*
    *   RGHandle：资源版本句柄（内部使用，公开仅为调试展示）。
    *   slot 记录 RGResource 槽位（0xFFFFFFFF = 无效），version 记录该槽位的版本号
    *   （每次写入 +1，SSA 式），两者共同精确追踪"读/写的是哪一版"。
    */
    struct ID_API RGHandle
    {
        uint32_t slot    = 0xFFFFFFFF;   // RGResource 槽位
        uint32_t version = 0;            // 该槽位的版本号

        bool is_valid() const { return slot != 0xFFFFFFFF; }
    };
} // namespace ID
