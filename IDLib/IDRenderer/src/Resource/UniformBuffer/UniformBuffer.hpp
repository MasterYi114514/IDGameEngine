#pragma once

#include "Resource/UniformBuffer/UniformBufferCreateInfo.hpp"

#ifdef IDRENDERER_USE_OPENGL

#include <glad/glad.h>

namespace ID
{
    class UniformBuffer
    {
    public:
        UniformBuffer() = delete;
        UniformBuffer(const UniformBufferCreateInfo& create_info);
        ~UniformBuffer() { destroy(); }

        UniformBuffer(const UniformBuffer&) = delete;
        UniformBuffer& operator=(const UniformBuffer&) = delete;

        UniformBuffer(UniformBuffer&&) noexcept;
        UniformBuffer& operator=(UniformBuffer&&) noexcept;

        void destroy();

    public:
        GLuint      get_ubo()               const { return m_UBO; }
        uint32_t    get_binding_point()     const { return m_binding_point; }
        size_t      get_size()              const { return m_size; }

        // 更新 UBO 数据（从 offset 起，更新 size 字节）
        void update_data(const void* data, size_t size, size_t offset = 0);

    private:
        GLuint      m_UBO               = 0;
        uint32_t    m_binding_point     = 0;
        size_t      m_size              = 0;
    };
} // namespace ID

#endif // IDRENDERER_USE_OPENGL