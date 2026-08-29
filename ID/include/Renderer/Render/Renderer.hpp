#pragma once

#include "IDpch.hpp"
#include "IDMath.hpp"
#include "Renderer/Mesh/Model.hpp"
#include "Renderer/Light/Light.hpp"
#include "Renderer/Camera/Camera.hpp"
#include "Renderer/Render/RenderContext.hpp"
#include "Scene/Scene.hpp"
#include "Renderer/Render/RenderGraph/RenderGraph.hpp"

namespace ID::Renderer
{
    // 渲染路径：前向（默认） / 延迟（G-Buffer + 全屏光照）
    enum class RenderPath : uint8_t
    {
        Forward = 0,
        Deferred = 1
    };

    void ID_API submit(const Model& model, const Mat4& world_transform);
    void ID_API submit(const Light& light);

    void ID_API render(const Camera& camera, Scene* scene = nullptr,
        uint32_t window_width = 0, uint32_t window_height = 0, float time = 0.0f);

    void ID_API clear_submissions();

    RenderGraph& ID_API     get_render_graph();
    void         ID_API     reset_render_graph();

    const RendererStatistics& ID_API    get_statistics();
    void                      ID_API    reset_statistics();

    void ID_API set_visual_pipeline(bool shadow = true, bool skybox = false, bool post_process = true);

    /**
     *  @brief 设置渲染路径（Forward / Deferred）
     *  @param path 目标渲染路径
     *
     *  当前仅记录状态并输出日志，Step 7 将接入重装配逻辑。
     */
    void ID_API set_render_path(RenderPath path);

    /**
     *  @brief 获取当前渲染路径
     *  @return RenderPath 当前渲染路径（默认 Forward）
     */
    RenderPath ID_API get_render_path();

    /**
     *  @brief 获取"最终显示 FBO"（PostProcess 输出目标）
     *  @return FrameBufferID；无效表示尚未渲染（无窗口）
     *
     *  其颜色纹理可用于 ImGui Viewport 面板显示（见 RenderCommand::get_framebuffer_color_texture）。
     */
    FrameBufferID ID_API get_viewport_fb();

    /**
     *  @brief 获取 G-Buffer FBO（延迟路径的几何输出目标）
     *  @return FrameBufferID；无效表示尚未渲染（无窗口）或非延迟路径
     *
     *  其三个颜色附件纹理可用于 ImGui G-Buffer 调试预览
     *  （见 RenderCommand::get_framebuffer_color_texture）。
     */
    FrameBufferID ID_API get_gbuffer_fb();
} // namespace ID::Renderer