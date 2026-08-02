#pragma once

#include "Core/IDRpch.hpp"
#include "Resource/VertexBuffer/VertexBufferCreateInfo.hpp"

#ifdef IDRENDERER_USE_OPENGL

#include <glad/glad.h>

namespace ID
{
    class VertexBuffer
    {
    public:
        VertexBuffer(const VertexBufferCreateInfo& create_info);
        ~VertexBuffer() { destroy(); }

        // 禁止拷贝
        VertexBuffer(const VertexBuffer&)               = delete;
        VertexBuffer& operator=(const VertexBuffer&)    = delete;

        // 允许移动（Vector 扩容时需要）
        VertexBuffer(VertexBuffer&&);
        VertexBuffer& operator=(VertexBuffer&&);

        // 销毁顶点缓冲对象
        void destroy();

    public:
        GLuint      get_vbo()           const { return m_VBO; }
        uint32_t    get_vertex_count()  const { return m_vertex_count; }

    private:
        GLuint m_VBO;                 // 顶点缓冲对象
        uint32_t m_vertex_count;      // 顶点的数量
    };
} // namespace ID

#endif

#ifdef IDRENDERER_USE_VULKAN

#include <vulkan/vulkan.h>

namespace ID
{
    class VertexBuffer
    {
    public:
        VertexBuffer(const VertexBufferCreateInfo& create_info);
        ~VertexBuffer() { destory(); }

        // 禁止拷贝
        VertexBuffer(const VertexBuffer&)               = delete;
        VertexBuffer& operator=(const VertexBuffer&)    = delete;

        // 允许移动（Vector 扩容时需要）
        VertexBuffer(VertexBuffer&&);
        VertexBuffer& operator=(VertexBuffer&&);

        // 销毁顶点缓冲对象
        void destroy();

        VkBuffer get_buffer() const { return m_buffer; }
        VkDeviceMemory get_buffer_memory() const { return m_buffer_memory; }
        uint32_t get_vertex_count() const { return m_vertex_count; }

    private:
        VkBuffer            m_buffer = VK_NULL_HANDLE;  // 顶点缓冲对象
        VkDeviceMemory      m_buffer_memory = VK_NULL_HANDLE; // 顶点缓冲对象
        uint32_t            m_vertex_count;      // 顶点的数量
    };
} // namespace ID

#endif

#ifdef IDRENDERER_USE_DX12
#endif