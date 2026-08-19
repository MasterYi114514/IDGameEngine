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

        /// 获取当前阴影贴图的 FBO（供 ForwardPass 查询）
        FrameBufferID get_shadow_fb() const;
        
        /// 获取当前光源 VP 矩阵
        const Mat4& get_light_view_proj() const { return m_light_view_proj; }

    public:
        void execute(RenderContext& ctx) override;

    private:
        void ensure_resources(uint32_t map_size);
        PipelineID get_depth_pipeline(const VertexBufferLayout& layout);
        void draw_depth_batch(RenderContext& ctx, const ModelSE& entry);
        bool find_directional_light(const RenderContext& ctx, Light& out_light) const;
        static uint64_t hash_layout(const VertexBufferLayout& layout);

    private:
        bool m_enabled = true;

        // 虚拟相机：负责计算光源视角的 view/proj
        DirectionalShadowCamera m_shadow_camera;

        // 阴影贴图句柄（通过 ShadowManager 池管理）
        ShadowMapID m_shadow_map_id = ShadowMapID::invalid_id();

        // 深度 shader（跨帧复用）
        ShaderID m_depth_shader = ShaderID::invalid_id();

        // 本帧计算结果（写回 ctx）
        Mat4 m_light_view_proj = Math::get_identity_mat4();

        // Pipeline 缓存
        std::unordered_map<uint64_t, PipelineID> m_pipeline_cache;
    };
} // namespace ID