#include "Resource/VertexBuffer/VertexBuffer.hpp"

#ifdef IDRENDERER_USE_OPENGL

namespace ID
{
    VertexBuffer::VertexBuffer(const VertexBufferCreateInfo& create_info)
        : m_vertex_count(create_info.vertex_count)
    {
        auto& layout = create_info.layout;

        glGenBuffers(1, &m_VBO);
        glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
        glBufferData(GL_ARRAY_BUFFER, layout.get_stride() * create_info.vertex_count, create_info.vertex_data, GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    VertexBuffer::VertexBuffer(VertexBuffer&& other) : m_VBO(other.m_VBO), m_vertex_count(other.m_vertex_count)
    {
        other.m_VBO = 0;
        other.m_vertex_count = 0;
    }

    VertexBuffer& VertexBuffer::operator=(VertexBuffer&& other)
    {
        if (this != &other)
        {
            std::swap(m_VBO, other.m_VBO);
            std::swap(m_vertex_count, other.m_vertex_count);

            other.destroy();
        }
        return *this;
    }

    void VertexBuffer::destroy()
    {
        glDeleteBuffers(1, &m_VBO);
        m_VBO = 0;
        m_vertex_count = 0;
    }
}

#endif