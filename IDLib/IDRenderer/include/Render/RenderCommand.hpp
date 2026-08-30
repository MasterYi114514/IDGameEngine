#pragma once

#include "Core/IDRpch.hpp"
#include "Resource/Shader/ShaderParamConcept.hpp"
#include "Resource/ResourceID.hpp"
#include "Resource/Pipeline/PipelineState.hpp"
#include "Resource/VertexBuffer/VertexBufferLayout.hpp"

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
    void IDR_API bind_default_framebuffer();    

    // 绑定 FB 颜色附件为采样纹理
    void IDR_API bind_framebuffer_color(const FrameBufferID framebuffer, 
        uint32_t attachment, uint32_t slot);
    
    // 绑定 FB 深度附件为采样纹理
    void IDR_API bind_framebuffer_depth(const FrameBufferID framebuffer, uint32_t slot);

    // 将外部纹理的第 layer 层 attach 到 framebuffer 的深度附件（array 深度纹理逐层渲染用）。
    // 语义：只改 FBO 当前深度附件指向，FBO 不持有该纹理所有权——生命周期由调用方管理。
    void IDR_API attach_framebuffer_depth_layer(const FrameBufferID framebuffer,
        const TextureID texture, uint32_t layer);

    // 将 src 帧缓冲的颜色附件 blit 到 dst 帧缓冲（尺寸相同）
    void IDR_API blit_framebuffer(const FrameBufferID src, const FrameBufferID dst,
        uint32_t width, uint32_t height);

    // 将帧缓冲的颜色附件 blit 到默认 framebuffer（窗口显示）
    void IDR_API blit_framebuffer_to_default(const FrameBufferID src,
        uint32_t width, uint32_t height);

    // 将 src 帧缓冲的深度附件 blit 到 dst 帧缓冲（尺寸相同；延迟路径：G-Buffer 深度 → 场景 FBO）
    void IDR_API blit_framebuffer_depth(const FrameBufferID src, const FrameBufferID dst,
        uint32_t width, uint32_t height);

    // 获取帧缓冲颜色附件的原生纹理句柄（供 ImGui::Image 等外部系统采样显示）
    uint32_t IDR_API get_framebuffer_color_texture(const FrameBufferID framebuffer);

    // 获取帧缓冲指定颜色附件的原生纹理句柄（G-Buffer 多附件预览用）
    uint32_t IDR_API get_framebuffer_color_texture(const FrameBufferID framebuffer,
        uint32_t attachment);

    // 绑定管线
    void IDR_API bind_pipeline(const PipelineID pipeline);
    void IDR_API bind_shader(const ShaderID shader);

    // 获取管线的顶点布局（供"同 layout 换 shader"的管线派生，如 GBufferPass）
    const VertexBufferLayout& IDR_API get_pipeline_layout(const PipelineID pipeline);

    // 获取管线状态（同上）
    const PipelineState& IDR_API get_pipeline_state(const PipelineID pipeline);

    // 按照 IBO 绘制顶点
    void IDR_API draw_indexed(const PipelineID pipeline, const VertexBufferID vb, const IndexBufferID ib);

    // 默认绘制 VBO 中的所有顶点
    void IDR_API draw_arrays(const PipelineID pipeline, const VertexBufferID vb);

    // 绘制 VBO 中指定范围的顶点
    void IDR_API draw_arrays(const PipelineID pipeline, const VertexBufferID vb, 
        uint32_t first_vertex, uint32_t vertex_count);


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

    // 绑定 sampler object 到纹理槽（sampler 状态优先于纹理对象参数；cmp/raw 双采样状态用）
    void IDR_API bind_sampler(const SamplerID sampler, uint32_t slot);
    void IDR_API unbind_sampler(uint32_t slot);

    // 绑定 UBO 到指定绑定点（glBindBufferBase；与 shader layout(std140, binding=N) 对应）
    void IDR_API bind_uniform_buffer(const UniformBufferID ub, uint32_t binding_point);

    // 更新 UBO 数据（从 offset 起更新 size 字节；glBufferSubData，调用方确保 ub 有效）
    void IDR_API update_uniform_buffer(const UniformBufferID ub,
        const void* data, size_t size, size_t offset = 0);

} // namespace ID::RenderCommand