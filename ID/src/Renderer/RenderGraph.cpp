#include "Renderer/RenderGraph.hpp"

namespace ID
{
    RenderGraph::RenderGraph() = default;

    RenderGraph::~RenderGraph() = default;

    void RenderGraph::clear()
    {
        m_passes.clear();
    }

    void RenderGraph::execute(RenderContext& ctx)
    {
        for (auto& pass : m_passes)
        {
            pass->execute(ctx);
        }
    }
} // namespace ID