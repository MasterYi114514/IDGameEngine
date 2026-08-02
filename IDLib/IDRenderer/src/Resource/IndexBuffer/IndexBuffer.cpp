#include "Resource/IndexBuffer/IndexBuffer.hpp"

#ifdef IDRENDERER_USE_OPENGL

namespace ID
{
    IndexBuffer::IndexBuffer(const IndexBufferCreateInfo& create_info) : m_index_count(create_info.index_count)
    {
        glGenBuffers(1, &m_index_id);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_index_id);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_index_count * sizeof(uint32_t), create_info.index_data, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }

    IndexBuffer::IndexBuffer(IndexBuffer&& other) : m_index_id(other.m_index_id), m_index_count(other.m_index_count)
    {
        other.m_index_id = 0;
        other.m_index_count = 0;
    }

    IndexBuffer& IndexBuffer::operator=(IndexBuffer&& other)
    {
        if(this != &other)
        {
            std::swap(m_index_id, other.m_index_id);
            std::swap(m_index_count, other.m_index_count);

            other.destroy();
        }
        return *this;
    }

    void IndexBuffer::destroy()
    {
        if(m_index_id != 0)
        {
            glDeleteBuffers(1, &m_index_id);
            m_index_id = 0;
            m_index_count = 0;
        }
    }
} // namespace ID

#endif