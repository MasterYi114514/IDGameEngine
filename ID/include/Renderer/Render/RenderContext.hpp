#pragma once

#include "IDpch.hpp"
#include "Renderer/Camera/Camera.hpp"
#include "Renderer/Light/Light.hpp"
#include "Renderer/Shadow/ShadowConfig.hpp"   // MAX_CASCADES
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

        // G-Buffer 渲染目标（延迟路径：GBufferPass 写入，LightingPass 读取）
        FrameBufferID              gbuffer_fb = FrameBufferID::invalid_id();

        // HDR 渲染目标
        FrameBufferID              scene_fb = FrameBufferID::invalid_id();

        // 最终显示目标（PostProcess 输出；无后处理时由 scene_fb 拷贝填充，供窗口 / ImGui Viewport 显示）
        FrameBufferID              viewport_fb = FrameBufferID::invalid_id();
        
        // 阴影贴图渲染目标
        FrameBufferID              shadow_fb = FrameBufferID::invalid_id();          // RenderGraph 依赖声明引用（语义不变）
        TextureID                  shadow_depth_array = TextureID::invalid_id();     // 阴影深度 array 纹理（采样绑定用，ShadowPass 写回）
        SamplerID                  shadow_sampler_raw = SamplerID::invalid_id();    // 阴影 raw 采样器（NEAREST，blocker search 用）
        SamplerID                  shadow_sampler_cmp = SamplerID::invalid_id();    // 阴影 cmp 采样器（LINEAR + REF_TO_TEXTURE，硬件 PCF）
        UniformBufferID            shadow_ubo = UniformBufferID::invalid_id();      // 阴影 ShadowBlock UBO（P9；消费点 bind binding 0）
        bool                       shadow_enabled = false;      // 此帧是否启用阴影渲染

        // 级联阴影（ShadowPass 逐层写回；P8 起 shader 消费。复位由 ShadowPass::execute 开头统一执行）
        uint32_t                   cascade_count   = 1;                              // 实际渲染层数（1 = 关闭 CSM）
        Mat4                       light_view_projs[MAX_CASCADES];                   // 各层光源 VP（逐层写回）
        float                      cascade_splits[MAX_CASCADES];                     // 各层远边界（视空间距离，正数）
        float                      cascade_bias_scales[MAX_CASCADES];                // 各层 bias 缩放（texel 尺寸维度；复位段填 1.0f）

        // 旧单级联字段（= 层 0 值；P8 前 shader 过渡期可用）
        Mat4                       light_view_proj = Math::get_identity_mat4();   // 光源视图投影矩阵
        float                      shadow_bias = 0.002f;       // 阴影深度偏移（防止自阴影）
        int                        shadow_pcf_radius = 1;       // PCF 采样半径（0→1×1, 1→3×3, 2→5×5, 3→7×7）
        int                        shadow_light_index = -1;      // 主方向光在光源数组中的下标（-1 表示无）
    };
} // namespace ID