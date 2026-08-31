#include "Resource/FrameBuffer/FrameBuffer.hpp"
#include "Log/Log.hpp"

#ifdef IDRENDERER_USE_OPENGL

namespace ID
{
    FrameBuffer::FrameBuffer(const FrameBufferCreateInfo& create_info)
        : m_width(create_info.width), m_height(create_info.height),
        m_color_texs(create_info.color_formats.size(), 0)
    {
        // 创建 FBO
        glGenFramebuffers(1, &m_FBO);
        glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);

        // 循环创建颜色附件纹理（MRT：多渲染目标）
        for (size_t i = 0; i < m_color_texs.size(); ++i)
        {
            glGenTextures(1, &m_color_texs[i]);
            glBindTexture(GL_TEXTURE_2D, m_color_texs[i]);

            bool hdr = (create_info.color_formats[i] == TextureFormat::RGBA16F);

            if(create_info.color_formats[i] == TextureFormat::SRGB8_ALPHA8)
            {
                // 渲染到 sRGB 附件会触发隐式编码，后处理链不需要；sRGB 仅用于采样侧纹理
                IDR_WARN("FrameBuffer：颜色附件使用 SRGB8_ALPHA8 属误用（渲染到 sRGB 附件会隐式编码），建议改用 RGBA8/RGBA16F");
            }

            if (create_info.samples > 1)
            {
                glTexImage2DMultisample
                (
                    GL_TEXTURE_2D_MULTISAMPLE,
                    static_cast<int>(create_info.samples), 
                    hdr ? GL_RGBA16F : GL_RGBA8,
                    static_cast<int>(m_width), 
                    static_cast<int>(m_height),
                    GL_TRUE
                );

                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(i),
                    GL_TEXTURE_2D_MULTISAMPLE, m_color_texs[i], 0);
            }
            else
            {
                glTexImage2D(GL_TEXTURE_2D, 0, hdr ? GL_RGBA16F : GL_RGBA8,
                    static_cast<int>(m_width), static_cast<int>(m_height),
                    0, GL_RGBA, hdr ? GL_FLOAT : GL_UNSIGNED_BYTE, nullptr);

                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(i), GL_TEXTURE_2D, m_color_texs[i], 0);
            }
        }

        // 创建深度附件纹理（如果需要）
        if (create_info.has_depth_attachment)
        {
            glGenTextures(1, &m_depth_tex);
            glBindTexture(GL_TEXTURE_2D, m_depth_tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, m_width, m_height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
            float border_color[] = { 1.0f, 1.0f, 1.0f, 1.0f };
            glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border_color);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_depth_tex, 0);
        }

        // 设置 draw buffers：MRT 必需步骤（FBO 默认 draw buffer 只有 COLOR_ATTACHMENT0，不调用则 RT1/RT2 无输出）
        if (!m_color_texs.empty())
        {
            std::vector<GLenum> draw_buffers(m_color_texs.size());
            for (size_t i = 0; i < m_color_texs.size(); ++i)
            {
                draw_buffers[i] = GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(i);
            }
            glDrawBuffers(static_cast<GLsizei>(draw_buffers.size()), draw_buffers.data());
        }
        else
        {
            // 纯深度 FBO：无颜色输出
            glDrawBuffer(GL_NONE);
        }

        // 检查 FBO 是否完整。无任何附件时跳过：该形态供外部 attach 深度层的调用方使用
        // （如 ShadowMap 的 array 深度 FBO，完整性由 attach 后保证）
        if (!m_color_texs.empty() || m_depth_tex != 0)
        {
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            {
                IDR_ERROR("OpenGL实现下，Framebuffer 不完整");
            }
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

        if (m_color_texs.size() > 0)
        {
            glDeleteTextures(static_cast<GLsizei>(m_color_texs.size()), m_color_texs.data());
            m_color_texs.clear();
        }

        if (m_depth_tex != 0)
        {
            glDeleteTextures(1, &m_depth_tex);
            m_depth_tex = 0;
        }
    }

    FrameBuffer::FrameBuffer(FrameBuffer&& other) noexcept
        : m_width(other.m_width), m_height(other.m_height), 
        m_FBO(other.m_FBO), m_color_texs(std::move(other.m_color_texs)), m_depth_tex(other.m_depth_tex)
    {
        other.m_width = 0;
        other.m_height = 0;
        other.m_FBO = 0;
        // m_color_texs 已移动，other 的 vector 为空
        other.m_depth_tex = 0;
    }

    FrameBuffer& FrameBuffer::operator=(FrameBuffer&& other) noexcept
    {
        if (this != &other)
        {
            std::swap(m_width, other.m_width);
            std::swap(m_height, other.m_height);
            std::swap(m_FBO, other.m_FBO);
            std::swap(m_color_texs, other.m_color_texs);
            std::swap(m_depth_tex, other.m_depth_tex);

            other.destroy();
        }
        return *this;
    }


} // namespace ID

#endif