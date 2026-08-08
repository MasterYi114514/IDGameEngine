#pragma once

#include "IDpch.hpp"
#include "Renderer/Camera/Camera.hpp"
#include "Renderer/Light/Light.hpp"
#include "Renderer/IDRCore.hpp"
#include "Renderer/Mesh/Model.hpp"

// 用于更新 statistics 的宏
#ifdef _ID_DEBUG
    #define ID_RS_INC_DRAW_CALLS(ctx)      if(ctx.statistics) { ctx.statistics->draw_calls++; }
    #define ID_RS_INC_TRIANGLES(ctx, n)    if(ctx.statistics) { ctx.statistics->triangles += (n); }
    #define ID_RS_INC_LIGHTS(ctx, n)       if(ctx.statistics) { ctx.statistics->lights += (n); }
    #define ID_RS_INC_OPAQUE(ctx)          if(ctx.statistics) { ctx.statistics->opaque++; }
    #define ID_RS_INC_TRANSPARENT(ctx)     if(ctx.statistics) { ctx.statistics->transparent++; }
#else
    #define ID_RS_INC_DRAW_CALLS(ctx)
    #define ID_RS_INC_TRIANGLES(ctx, n)
    #define ID_RS_INC_LIGHTS(ctx, n)
    #define ID_RS_INC_OPAQUE(ctx)
    #define ID_RS_INC_TRANSPARENT(ctx)
#endif


namespace ID
{
    // 一帧的渲染统计信息
    struct RendererStatistics
    {
        uint32_t draw_calls   = 0;      // 绘制调用次数
        uint32_t triangles    = 0;      // 三角形总数
        uint32_t lights       = 0;      // 参与本帧渲染的光源数
        uint32_t opaque       = 0;      // 不透明批次
        uint32_t transparent  = 0;      // 透明批次
    };

    // 提交条目
    template<typename T>
    struct SubmitEntry;

    class Model;

    template<>
    struct ID_API SubmitEntry<Model>
    {
        SubmitEntry(const Model& model, const Mat4& outer_transform, const PipelineID pipeline_id);

        const MaterialInstance* material = nullptr;                             // 材质实例
        MeshID                  mesh = MeshID::invalid_id();                    // 网格
        Mat4                    world_transform = Math::get_identity_mat4();    // 世界变换矩阵
        PipelineID              pipeline = PipelineID::invalid_id();            // 渲染管线
        float                   view_distance_sq = 0.0f;                        // 相机距离平方，用于排序
    };

    template<>
    struct ID_API SubmitEntry<Light>
    {
        SubmitEntry(const Light& light) : light(&light) { }
        const Light* light = nullptr;                      // 光源

        // 提供方法，允许像访问 Light 成员一样访问 SubmitEntry<Light>
        const Light* operator->() const { return light; }
        const Light& operator*() const { return *light; }
    };

    using ModelSE = SubmitEntry<Model>;
    using LightSE = SubmitEntry<Light>;

    /**
     *  RenderContext：渲染上下文，传递给 RenderPass 的参数
     *  RenderContext 是全体共享的数据
     */
    struct RenderContext
    {
        const Camera&              camera;
        uint32_t                   window_width    = 0;
        uint32_t                   window_height   = 0;
        float                      time            = 0.0f;

        std::vector<ModelSE>&      opaque_batches;          // 不透明批次（已排序）
        std::vector<ModelSE>&      transparent_batches;     // 透明批次（已排序）
        std::vector<LightSE>&      lights;                  // 光源列表

        RendererStatistics*        statistics = nullptr;    // 统计（可空）

        // HDR 渲染目标
        FrameBufferID              scene_fb = FrameBufferID::invalid_id();
        
        // 阴影贴图渲染目标
        FrameBufferID              shadow_fb = FrameBufferID::invalid_id();
        bool                       shadow_enabled = false;      // 此帧是否启用阴影渲染
        Mat4                       light_view_proj = Math::get_identity_mat4();   // 光源视图投影矩阵
    };
} // namespace ID