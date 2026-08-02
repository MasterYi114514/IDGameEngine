#include "Resource/FrameBuffer/FrameBuffer.hpp"
#include "Log/Log.hpp"

#ifdef IDRENDERER_USE_OPENGL

namespace ID
{
    FrameBuffer::FrameBuffer(const FrameBufferCreateInfo& create_info) : m_width(create_info.width), m_height(create_info.height)
    {
        // 创建 FBO
        glGenFramebuffers(1, &m_FBO);
        glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);

        // 创建颜色附件纹理
        glGenTextures(1, &m_color_tex);
        glBindTexture(GL_TEXTURE_2D, m_color_tex);

        if (create_info.samples > 1)
        {
            glTexImage2DMultisample
            (
                GL_TEXTURE_2D_MULTISAMPLE,
                static_cast<int>(create_info.samples), 
                GL_RGBA8,
                static_cast<int>(m_width), 
                static_cast<int>(m_height),
                GL_TRUE
            );

            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_2D_MULTISAMPLE, m_color_tex, 0);
        }
        else
        {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                static_cast<int>(m_width), static_cast<int>(m_height),
                0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_color_tex, 0);
        }

        // 创建深度附件纹理（如果需要）
        if (create_info.has_depth_attachment)
        {
            glGenTextures(1, &m_depth_tex);
            glBindTexture(GL_TEXTURE_2D, m_depth_tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, m_width, m_height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_depth_tex, 0);
        }

        // 检查 FBO 是否完整
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            IDR_ERROR("OpenGL实现下，Framebuffer 不完整");
        }

        // 解绑 FBO 与 纹理
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void FrameBuffer::destroy()
    {
        if (m_FBO != 0)
        {
            glDeleteFramebuffers(1, &m_FBO);
            m_FBO = 0;
        }

        if (m_color_tex != 0)
        {
            glDeleteTextures(1, &m_color_tex);
            m_color_tex = 0;
        }

        if (m_depth_tex != 0)
        {
            glDeleteTextures(1, &m_depth_tex);
            m_depth_tex = 0;
        }
    }

    FrameBuffer::FrameBuffer(FrameBuffer&& other) noexcept
        : m_width(other.m_width), m_height(other.m_height), 
        m_FBO(other.m_FBO), m_color_tex(other.m_color_tex), m_depth_tex(other.m_depth_tex)
    {
        other.m_width = 0;
        other.m_height = 0;
        other.m_FBO = 0;
        other.m_color_tex = 0;
        other.m_depth_tex = 0;
    }

    FrameBuffer& FrameBuffer::operator=(FrameBuffer&& other) noexcept
    {
        if (this != &other)
        {
            std::swap(m_width, other.m_width);
            std::swap(m_height, other.m_height);
            std::swap(m_FBO, other.m_FBO);
            std::swap(m_color_tex, other.m_color_tex);
            std::swap(m_depth_tex, other.m_depth_tex);

            other.destroy();
        }
        return *this;
    }


} // namespace ID

#endif