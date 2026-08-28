#pragma once

// =====================================================================
//  RenderGraph 内部头（不对外）
//  与公开头 include/Renderer/Render/RenderGraph/RenderGraph.hpp 同名。
//  仅限本目录下的 .cpp 以引号形式 #include "RenderGraph.hpp" 包含
//  （引号包含优先搜索当前文件目录，命中本文件而非公开头）；
//  禁止以 "Renderer/Render/RenderGraph/RenderGraph.hpp" 路径形式包含
//  （该形式会先命中 include/ 根下的公开头）。
// =====================================================================

#include "IDpch.hpp"
#include "Renderer/Render/RenderGraph/RenderGraph.hpp"
#include "Renderer/Render/RenderGraph/RenderPassBuilder.hpp"

namespace ID
{
    /*
    *   RGEdgeType：依赖边类型（与 4.4 建边规则一一对应，GraphViz 导出标注用）
    */
    enum class RGEdgeType : uint8_t
    {
        RAW,       // Read After Write：读当前版本的写入者
        WAW,       // Write After Write：写写不乱序
        WAR,       // Write After Read：不能覆盖别人正在读的数据
        Order,     // 保序边：读-读保序（依赖之前的读者）
        Explicit   // 显式 after() 边（逃生舱，可指向前方，环检测由此而生）
    };

    /*
    *   RGPassEdge：带类型的依赖边（node 为对端节点索引，type 为边类型）
    */
    struct RGPassEdge
    {
        uint32_t   node = 0xFFFFFFFF;          // 对端节点索引
        RGEdgeType type = RGEdgeType::RAW;     // 边类型
    };

    /*
    *   RGPassNode：Pass 节点，图的一个顶点。
    *   reads / writes 在 setup 阶段由 RenderPassBuilder 收拢写入（记录读/写的版本）；
    *   preds / succs 由 derive_edges 按 RAW/WAW/WAR/保序/显式 after 规则推导。
    */
    struct RGPassNode
    {
        std::unique_ptr<RenderPass> pass;                    // 多态持有（渲染层允许继承）
        std::string                 name = "RenderPass";     // 调试用名字
        std::vector<RGHandle>       reads;                   // 声明读取的资源版本
        std::vector<RGHandle>       writes;                  // 声明写入的资源版本
        std::vector<RGPassEdge>     preds;                   // 推导出的前驱边（依赖谁）
        std::vector<RGPassEdge>     succs;                   // 后继边（被谁依赖）
        uint32_t                    indegree = 0;            // 入度（Kahn 排序用）
        bool                        culled   = false;        // 死 Pass 剔除标记（Step 2 起）
    };

    /*
    *   RGResourceNode：资源节点，跟踪每个语义槽位的版本时间线。
    *   last_writer / readers 描述"当前版本"的读写者；每次写入 version +1。
    */
    struct RGResourceNode
    {
        std::string           name;              // "ShadowMap" 等，日志/导出用
        int32_t               last_writer = -1;  // 当前版本写入者的节点索引
        std::vector<uint32_t> readers;           // 当前版本的读者
        uint32_t              version     = 0;   // 当前版本号
    };
} // namespace ID
