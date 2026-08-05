#pragma once

#include "IDpch.hpp"
#include "Renderer/Camera/Camera.hpp"
#include "Renderer/Light/Light.hpp"
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

    // 提交条目：单帧提交条目，用于登记一个要画的物体
    struct SubmitEntry
    {
        Model        model;                         // 可绘制单元（值拷贝；内部有 ShaderID）
        Mat4         world_transform;               // 世界矩阵（含模型空间偏移）
        PipelineID   pipeline;                      // 提交时由 Renderer 缓存计算
        float        view_distance_sq = 0.0f;       // 相机距离平方（render 时填充）
    };

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

        std::vector<SubmitEntry>&  opaque_batches;          // 不透明批次（已排序）
        std::vector<SubmitEntry>&  transparent_batches;     // 透明批次（已排序）
        std::vector<Light>&        lights;                  // 光源列表
        RendererStatistics*        statistics = nullptr;    // 统计（可空）
    };
} // namespace ID