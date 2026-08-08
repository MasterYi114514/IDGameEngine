#pragma once

#include "IDpch.hpp"
#include "Renderer/Render/RenderPass/RenderPass.hpp"
#include "Renderer/Render/RenderContext.hpp"

namespace ID
{
    /**
     *  后处理效果位标志：可链式组合（按位或）
     */
    enum PostProcessEffect : uint32_t
    {
        PostProcessEffect_None        = 0,
        PostProcessEffect_Bloom       = 1 << 0,   // 泛光
        PostProcessEffect_ToneMapping = 1 << 1,   // HDR → LDR（ACES Filmic）
        PostProcessEffect_Gamma       = 1 << 2,   // Gamma 校正
    };

    /**
     *  PostProcessPass：链式后处理 Pass（Phase 4 新增）
     *
     *  输入：ctx.scene_fb（ForwardPass 输出的 HDR 场景，RGBA16F）
     *  输出：m_output_fb（无效 = 默认屏幕）
     *
     *  每个效果是一个全屏三角形 Pass：
     *    [Bloom]  extract:  输入 → bloom_a（亮部提取，阈值截取）
     *    [Bloom]  blur_h:   bloom_a → bloom_b（5-tap 高斯，水平）
     *    [Bloom]  blur_v:   bloom_b → bloom_a（垂直）
     *    composite:         输入 + bloom_a → 输出（含 ACES ToneMapping + Gamma，按位控制）
     *
     *  bloom 中间缓冲为 1/4 分辨率 RGBA16F，窗口 resize 时自动重建。
     */
    class ID_API PostProcessPass : public RenderPass
    {
    public:
        /**
         *  @param output_fb        输出目标（无效 = 默认屏幕）
         *  @param effects          效果位组合，默认 ToneMapping | Gamma（画面"正确"的最低配置）
         *  @param bloom_threshold  亮部提取亮度阈值
         *  @param bloom_strength   泛光强度（composite 时叠加系数）
         */
        PostProcessPass(FrameBufferID output_fb = FrameBufferID::invalid_id(),
            uint32_t effects = PostProcessEffect_ToneMapping | PostProcessEffect_Gamma,
            float bloom_threshold = 1.0f, float bloom_strength = 0.8f);
        virtual ~PostProcessPass() override = default;

    public:
        void set_output_fb(FrameBufferID fb) { m_output_fb = fb; }
        FrameBufferID get_output_fb() const { return m_output_fb; }

        void set_effects(uint32_t effects) { m_effects = effects; }
        uint32_t get_effects() const { return m_effects; }

        void set_bloom_threshold(float threshold) { m_bloom_threshold = threshold; }
        void set_bloom_strength(float strength) { m_bloom_strength = strength; }

    public:
        virtual void execute(RenderContext& ctx) override;

    private:
        // 懒创建：postprocess shader + pipeline + bloom 中间 FBO（1/4 分辨率，resize 重建）
        void ensure_resources(uint32_t window_w, uint32_t window_h);
        // 绑定目标 + 视口，绘制全屏三角形
        void render_fullscreen(PipelineID pipeline, FrameBufferID target, uint32_t w, uint32_t h);

        // 子步骤
        void bloom_extract(RenderContext& ctx, FrameBufferID src);
        void bloom_blur(RenderContext& ctx, FrameBufferID src, FrameBufferID dst, bool horizontal);
        void composite(RenderContext& ctx, FrameBufferID src, FrameBufferID bloom_src, FrameBufferID dst);

    private:
        FrameBufferID  m_output_fb;                 // 输出目标（无效 = 默认屏幕）
        uint32_t       m_effects;                   // 效果位组合
        float          m_bloom_threshold = 1.0f;
        float          m_bloom_strength  = 0.8f;

        ShaderID       m_shader   = ShaderID::invalid_id();
        PipelineID     m_pipeline = PipelineID::invalid_id();

        FrameBufferID  m_bloom_a;                   // bloom 中间缓冲（1/4 分辨率）
        FrameBufferID  m_bloom_b;
        uint32_t       m_bloom_w = 0;
        uint32_t       m_bloom_h = 0;
    };
} // namespace ID
