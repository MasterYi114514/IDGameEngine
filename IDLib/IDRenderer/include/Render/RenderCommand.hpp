#pragma once

#include "Core/IDRpch.hpp"
#include "Resource/Shader/ShaderParamConcept.hpp"
#include "Resource/ResourceID.hpp"

#include "IDMath.hpp"

namespace ID::RenderCommand
{
    // 清屏颜色
    void IDR_API set_clear_color(float r, float g, float b, float a = 1.0f);
    void IDR_API set_clear_color(const Vec3& color, float a = 1.0f);
    void IDR_API set_clear_color(const Vec4& color);

    void IDR_API clear(bool clear_color = true, bool clear_depth = true);

    // Viewport 设置
    void IDR_API set_viewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height);

    // 绑定渲染目标
    void IDR_API bind_framebuffer(const FrameBufferID framebuffer);

    // 绘制

    // 按照 IBO 绘制顶点
    void IDR_API draw_indexed(const PipelineID pipeline, const VertexBufferID vb, const IndexBufferID ib);

    // 默认绘制 VBO 中的所有顶点
    void IDR_API draw_arrays(const PipelineID pipeline, const VertexBufferID vb);

    // 绘制 VBO 中指定范围的顶点
    void IDR_API draw_arrays(const PipelineID pipeline, const VertexBufferID vb, 
        uint32_t first_vertex, uint32_t vertex_count);

    // Shader 的 参数设置，允许通过 pipeline 或 shader 设置参数

    template<typename... Args>
    requires ParamConcept::CanSetParam<Args...>
    void set_param(const PipelineID pipeline, 
        const std::string& param_name, const Args&... args);

    template<typename... Args>
    requires ParamConcept::CanSetParam<Args...>
    void set_param(const ShaderID shader, 
        const std::string& param_name, const Args&... args);

    template<typename IDVec>
    requires ParamConcept::IsVec<IDVec>
    void set_param(const PipelineID pipeline, 
        const std::string& param_name, const IDVec& vec);

    template<typename IDVec>
    requires ParamConcept::IsVec<IDVec>
    void set_param(const ShaderID shader,
        const std::string& param_name, const IDVec& vec);

    template<typename IDMat>
    requires ParamConcept::IsMat<IDMat>
    void set_param(const PipelineID pipeline, 
        const std::string& param_name, const IDMat& mat);

    template<typename IDMat>
    requires ParamConcept::IsMat<IDMat>
    void set_param(const ShaderID shader,
        const std::string& param_name, const IDMat& mat);

    // 不通过 IDMat 传入矩阵的方式，要求传入的矩阵是列优先排序
    void IDR_API set_param(const PipelineID pipeline, const std::string& param_name, 
        uint32_t rows, uint32_t cols, const float* data);
    void IDR_API set_param(const ShaderID shader, const std::string& param_name, 
        uint32_t rows, uint32_t cols, const float* data);

    void IDR_API bind_texture(const TextureID texture, uint32_t slot);
    void IDR_API unbind_texture(uint32_t slot);

} // namespace ID::RenderCommand