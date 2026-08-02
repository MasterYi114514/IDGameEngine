#include "Resource/UniformBuffer/UniformBuffer.hpp"
#include "Log/Log.hpp"

#ifdef IDRENDERER_USE_OPENGL

namespace
{
    GLenum to_gl_buffer_usage_hint(ID::BufferUsageHint usage_hint)
    {
        switch (usage_hint)
        {
            case ID::BufferUsageHint::None:         return GL_NONE;
            case ID::BufferUsageHint::StaticDraw:   return GL_STATIC_DRAW;
            case ID::BufferUsageHint::DynamicDraw:  return GL_DYNAMIC_DRAW;
            case ID::BufferUsageHint::StreamDraw:   return GL_STREAM_DRAW;
            default: return GL_DYNAMIC_DRAW;
        }
    }
} // 匿名命名空间

namespace ID
{
    UniformBuffer::UniformBuffer(const UniformBufferCreateInfo& create_info)
        : m_binding_point(create_info.binding_point), m_size(create_info.size)
    {
        glGenBuffers(1, &m_UBO);
        glBindBuffer(GL_UNIFORM_BUFFER, m_UBO);
        glBufferData(GL_UNIFORM_BUFFER, m_size, nullptr, to_gl_buffer_usage_hint(create_info.usage_hint));
        glBindBufferBase(GL_UNIFORM_BUFFER, m_binding_point, m_UBO);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }

    UniformBuffer::UniformBuffer(UniformBuffer&& other) noexcept
        : m_UBO(other.m_UBO), m_binding_point(other.m_binding_point), m_size(other.m_size)
    {
        other.m_UBO = 0;
        other.m_binding_point = 0;
        other.m_size = 0;
    }

    UniformBuffer& UniformBuffer::operator=(UniformBuffer&& other) noexcept
    {
        if (this != &other)
        {
            std::swap(m_UBO, other.m_UBO);
            std::swap(m_binding_point, other.m_binding_point);
            std::swap(m_size, other.m_size);

            other.destroy();
        }
        return *this;
    }

    void UniformBuffer::destroy()
    {
        if (m_UBO != 0)
        {
            glDeleteBuffers(1, &m_UBO);
            m_UBO = 0;
        }
        m_binding_point = 0;
        m_size = 0;
    }

    void UniformBuffer::update_data(const void* data, size_t size, size_t offset)
    {
        if (offset + size > m_size)
        {
            IDR_ERROR("UniformBuffer::update_data: 传入的数据大小超出缓冲区范围 (offset + size > m_size)");
            return;
        }

        glBindBuffer(GL_UNIFORM_BUFFER, m_UBO);
        glBufferSubData(GL_UNIFORM_BUFFER, offset, size, data);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }
} // namespace ID

#endif