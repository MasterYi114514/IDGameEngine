#include "Renderer/Render/RenderGraph/RenderPassBuilder.hpp"
#include "RenderGraph.hpp"   // 内部头（同目录引号包含，含公开 RenderGraph 类定义）

namespace ID
{
    RenderPassBuilder::RenderPassBuilder(RenderGraph& graph, uint32_t node_index)
        : m_graph(graph), m_node_index(node_index) { }

    void RenderPassBuilder::reads(RGResourceName res)
    {
        RGPassNode&           node     = m_graph.m_nodes[m_node_index];
        const RGResourceNode& res_node = m_graph.m_resources[static_cast<uint32_t>(res)];
        node.reads.push_back(RGHandle{ static_cast<uint32_t>(res), res_node.version });
    }

    void RenderPassBuilder::writes(RGResourceName res)
    {
        RGPassNode&     node     = m_graph.m_nodes[m_node_index];
        RGResourceNode& res_node = m_graph.m_resources[static_cast<uint32_t>(res)];
        node.writes.push_back(RGHandle{ static_cast<uint32_t>(res), res_node.version + 1 });
    }

    void RenderPassBuilder::read_writes(RGResourceName res)
    {
        reads(res);
        writes(res);
    }

    void RenderPassBuilder::after(const RenderPass& target)
    {
        m_after_targets.push_back(&target);
    }
} // namespace ID
