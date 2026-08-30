#pragma once 

#include "IDpch.hpp"
#include "Renderer/Render/RenderPass/RenderPass.hpp"
#include "Renderer/Render/RenderContext.hpp"
#include "Renderer/Shadow/ShadowCamera.hpp"
#include "Renderer/Shadow/ShadowMap.hpp"
#include "Renderer/Shadow/ShadowManager.hpp"

namespace ID
{
    class ID_API ShadowPass : public RenderPass
    {
    public:
        ShadowPass();
        virtual ~ShadowPass() override = default;

    public:
        // ── 开关 ──
        void set_enabled(bool enabled) { m_enabled = enabled; }
        bool is_enabled() const { return m_enabled; }

        // ── 访问内部组件（暴露给外部配置）──
        DirectionalShadowCamera& get_camera() { return m_shadow_camera; }
        const DirectionalShadowCamera& get_camera() const { return m_shadow_camera; }

        /// 获取当前阴影贴图的 FBO
        FrameBufferID get_shadow_fb() const;
        
        /// 获取当前光源 VP 矩阵
        const Mat4& get_light_view_proj() const { return m_light_view_proj; }

    public:
        // 声明依赖：写 ShadowMap（ctx.shadow_fb，供 Forward/Transparent 采样阴影）
        void setup(RenderPassBuilder& builder) override;

        void execute(RenderContext& ctx) override;

    private:
        void ensure_resources(uint32_t map_size);
        PipelineID get_depth_pipeline(const VertexBufferLayout& layout);
        void draw_depth_batch(RenderContext& ctx, const ModelSE& entry);
        bool find_directional_light(const RenderContext& ctx, Light& out_light) const;
        static uint64_t hash_layout(const VertexBufferLayout& layout);

        // 填充并上传 ShadowBlock UBO（enabled=false 时仅置 misc.z=0，防残留上帧数据）
        void upload_shadow_block(RenderContext& ctx, bool enabled);

    private:
        bool m_enabled = true;

        // 虚拟相机：负责计算光源视角的 view/proj
        DirectionalShadowCamera m_shadow_camera;

        // 阴影贴图句柄（通过 ShadowManager 池管理）
        ShadowMapID m_shadow_map_id = ShadowMapID::invalid_id();

        // 深度 array 层数（P7 起同步 RendererSettings::cascade_count；P4 固定 1）
        uint32_t m_shadow_layer_count = 1;

        // Sampler Object（硬件 PCF 双采样状态；ShadowPass 长命持有，ensure_resources 懒创建）
        SamplerID m_sampler_raw = SamplerID::invalid_id();   // NEAREST + 无比较（blocker search 读原始深度）
        SamplerID m_sampler_cmp = SamplerID::invalid_id();   // LINEAR + REF_TO_TEXTURE（硬件 2×2 比较双线性）

        // 阴影 UBO（ShadowBlock std140，binding 0；P9 起一次 glBufferSubData 上传替代逐元素 set_param）
        UniformBufferID m_shadow_ubo = UniformBufferID::invalid_id();

        // 深度 shader（跨帧复用）
        ShaderID m_depth_shader = ShaderID::invalid_id();

        // 本帧计算结果（写回 ctx）
        Mat4 m_light_view_proj = Math::get_identity_mat4();

        // 当前层 bias 缩放（texel 尺寸维度；逐层渲染时由 execute 设置，draw_depth_batch 消费）
        float m_layer_bias_scale = 1.0f;

        // Pipeline 缓存
        std::unordered_map<uint64_t, PipelineID> m_pipeline_cache;
    };
} // namespace ID