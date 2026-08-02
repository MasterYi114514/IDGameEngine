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

        // 预留下标参数，以便未来支持 MRT
        GLuint get_color_attachment(uint32_t index = 0) const { return index == 0 ? m_color_tex : 0; }

    private:
        uint32_t        m_width     = 0;
        uint32_t        m_height    = 0;

        GLuint          m_FBO       = 0;
        GLuint          m_color_tex = 0;
        GLuint          m_depth_tex = 0;
    };
} // namespace ID

#endif