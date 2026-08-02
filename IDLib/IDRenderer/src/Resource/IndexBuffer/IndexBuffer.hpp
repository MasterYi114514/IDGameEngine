#pragma once

#include "Resource/IndexBuffer/IndexBufferCreateInfo.hpp"

#ifdef IDRENDERER_USE_OPENGL

#include <glad/glad.h>

namespace ID
{
    class IndexBuffer
    {
    public:
        IndexBuffer(const IndexBufferCreateInfo& create_info);
        ~IndexBuffer() { destroy(); }

        // 禁止拷贝
        IndexBuffer(const IndexBuffer&)               = delete;
        IndexBuffer& operator=(const IndexBuffer&)    = delete;

        // 允许移动（Vector 扩容时需要）
        IndexBuffer(IndexBuffer&&);
        IndexBuffer& operator=(IndexBuffer&&);

        // 销毁索引缓冲对象
        void destroy();
    public:
        GLuint get_ibo() const { return m_index_id; }
        uint32_t get_index_count() const { return m_index_count; }

    private:
        GLuint      m_index_id;             // 索引缓冲对象
        uint32_t    m_index_count;          // 索引的数量
    };
} // namespace ID

#endif