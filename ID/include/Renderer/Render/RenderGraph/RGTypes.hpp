#pragma once

#include "IDpch.hpp"

namespace ID
{
    /*
    *   RGResourceName：语义资源槽位，与 RenderContext 中的字段一一对应。
    *   TODO: 将扩展为任意命名瞬态资源，此枚举届时退役。
    */
    enum class RGResourceName : uint8_t
    {
        ShadowMap,       // ctx.shadow_fb（由 ShadowPass 写入）
        GBuffer,         // ctx.gbuffer_fb（由 GBufferPass 写入，LightingPass 读取）
        SceneColor,      // ctx.scene_fb（Forward/Skybox/Transparent 读改写）
        ViewportTarget,  // ctx.viewport_fb（PostProcess 写入，最终呈现目标）

        Count
    };

    /*
    *   RGHandle：资源版本句柄
    *   slot 记录 RGResourceName 槽位（0xFFFFFFFF = 无效），version 记录该槽位的版本号
    *   （每次写入 +1，SSA 式），两者共同精确追踪"读/写的是哪一版"。
    */
    struct ID_API RGHandle
    {
        uint32_t slot    = 0xFFFFFFFF;   // RGResourceName 槽位
        uint32_t version = 0;            // 该槽位的版本号

        bool is_valid() const { return slot != 0xFFFFFFFF; }
    };

    /*
    *   RGEdgeType：依赖边类型。原定义于内部头 src/.../RenderGraph.hpp，
    *   因节点编辑器需要展示而迁移至此公开。
    */
    enum class RGEdgeType : uint8_t
    {
        RAW,       // Read After Write
        WAW,       // Write After Write
        WAR,       // Write After Read
        Order,     // 保序边：RAR
        Explicit   // 显式 after() 边
    };

    /* 
     *  RGPassView：单个 Pass 的绘制用只读快照
    */
    struct RGPassView
    {
        uint32_t              index      = 0;      // 节点索引（声明序）
        int32_t               exec_index = -1;     // 执行序，-1 = 未入执行序（被剔除）
        std::string           name;                // 调试名
        bool                  culled     = false;  // 死 Pass 剔除标记
        std::vector<RGResourceName> reads;             // 去重后的读槽位
        std::vector<RGResourceName> writes;            // 去重后的写槽位
    };

    /* RGEdgeView：一条依赖边（from → to，节点索引） */
    struct RGEdgeView
    {
        uint32_t   from = 0;
        uint32_t   to   = 0;
        RGEdgeType type = RGEdgeType::RAW;
    };

    /* RGGraphView：整图绘制用快照 */
    struct RGGraphView
    {
        std::vector<RGPassView>   passes;            // 全部 Pass（含被剔除的）
        std::vector<RGEdgeView>   edges;             // 全部依赖边
        std::vector<std::string>  execution_order;   // 编译产物（pass 名列表）
        std::vector<std::string>  resource_names;    // 槽位名（"ShadowMap" 等，下标 = RGResource）
    };
} // namespace ID
