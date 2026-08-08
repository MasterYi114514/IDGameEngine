#pragma once

#include "IDpch.hpp"
#include "IDMath.hpp"
#include "Renderer/Mesh/Model.hpp"
#include "Renderer/Light/Light.hpp"
#include "Renderer/Camera/Camera.hpp"
#include "Renderer/Render/RenderContext.hpp"
#include "Scene/Scene.hpp"
#include "Renderer/Render/RenderGraph.hpp"

namespace ID::Renderer
{
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
} // namespace ID::Renderer