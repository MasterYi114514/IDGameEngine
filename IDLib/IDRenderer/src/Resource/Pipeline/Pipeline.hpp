#pragma once

#include "Resource/Pipeline/PipelineCreateInfo.hpp"

#ifdef IDRENDERER_USE_OPENGL

#include <glad/glad.h>

namespace ID
{
    class Pipeline
    {
    public:
        Pipeline() = delete;
        Pipeline(const PipelineCreateInfo& createInfo);
        ~Pipeline() { destroy(); }

        // 禁止拷贝
        Pipeline(const Pipeline&) = delete;
        Pipeline& operator=(const Pipeline&) = delete;

        // 允许移动
        Pipeline(Pipeline&& other) noexcept;
        Pipeline& operator=(Pipeline&& other) noexcept;

        // destroy 方法
        void destroy();

    public:
        // 获取着色器ID
        ShaderID get_shader_id() const { return m_shaderID; }

        // 获取顶点布局
        const VertexBufferLayout& get_vertex_buffer_layout() const { return m_layout; }

        // 获取管线状态
        const PipelineState& get_pipeline_state() const { return m_pipelineState; }

        // 按照 VBO 动态创建/缓存 VAO
        GLuint get_vao(GLuint vbo) const;

    private:
        ShaderID                m_shaderID;         // 着色器ID
        VertexBufferLayout      m_layout;           // 顶点布局
        PipelineState           m_pipelineState;    // 管线状态

        mutable std::unordered_map<GLuint, GLuint> vaoMap;
    };
} // namespace ID

#endif