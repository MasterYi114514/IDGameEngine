#include "Renderer/Render/FullscreenQuad.hpp"
#include "Renderer/IDRCore.hpp"

namespace ID
{
    VertexBufferLayout& FullscreenQuad::layout()
    {
        static VertexBufferLayout s_layout = []()
        {
            VertexBufferLayout l;
            l.push(VertexBufferAttribute{ "aPos", AttributeType::Float2, false, 0 });
            return l;
        }();
        return s_layout;
    }

    VertexBufferID& FullscreenQuad::vertex_buffer()
    {
        // 覆盖屏幕的大三角形（比视口大，保证边缘像素都被覆盖）
        static const float s_vertices[] = {
            -1.0f, -1.0f,
             3.0f, -1.0f,
            -1.0f,  3.0f,
        };

        static VertexBufferID s_vb = []() -> VertexBufferID
        {
            return VBManager::create(VertexBufferCreateInfo(s_vertices, 3, layout()));
        }();
        return s_vb;
    }
} // namespace ID
