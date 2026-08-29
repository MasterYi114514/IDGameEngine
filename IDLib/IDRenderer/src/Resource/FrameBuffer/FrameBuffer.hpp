#pragma once

#include "Core/IDRpch.hpp"
#include "Resource/FrameBuffer/FrameBufferCreateInfo.hpp"

#ifdef IDRENDERER_USE_OPENGL

#include <glad/glad.h>

namespace ID
{
    class FrameBuffer
    {
    public:
        // 禁止默认构造
        FrameBuffer() = delete;

        FrameBuffer(const FrameBufferCreateInfo& create_info);
        ~FrameBuffer() { destroy(); }

        // 禁止拷贝
        FrameBuffer(const FrameBuffer&)             = delete;
        FrameBuffer& operator=(const FrameBuffer&)  = delete;

        // 允许移动
        FrameBuffer(FrameBuffer&& other) noexcept;
        FrameBuffer& operator=(FrameBuffer&& other) noexcept;

        void destroy();

    public:
        uint32_t    get_width()             const { return m_width; }
        uint32_t    get_height()            const { return m_height; }

        GLuint      get_FBO()               const { return m_FBO; }
        GLuint      get_depth_attachment()  const { return m_depth_tex; }

        // 颜色附件（MRT 下标访问，越界返回 0：GL 空纹理，采样得黑，便于发现错误）
        GLuint get_color_attachment(uint32_t index = 0) const
        {
            return index < m_color_texs.size() ? m_color_texs[index] : 0;
        }

        // 颜色附件数量（MRT）
        uint32_t get_color_attachment_count() const { return static_cast<uint32_t>(m_color_texs.size()); }

    private:
        uint32_t            m_width     = 0;
        uint32_t            m_height    = 0;

        GLuint              m_FBO       = 0;
        std::vector<GLuint> m_color_texs;   // 颜色附件纹理数组（MRT，按 color_formats 顺序创建）
        GLuint              m_depth_tex = 0;
    };
} // namespace ID

#endif