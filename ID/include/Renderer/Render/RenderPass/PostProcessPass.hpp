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
     *  PostProcessPass：链式后处理 Pass（Phase 4 升级：多级降采样金字塔 Bloom）
     *
     *  输入：ctx.scene_fb（ForwardPass 输出的 HDR 场景，RGBA16F）
     *  输出：m_output_fb（无效 = ctx.viewport_fb / 默认屏幕）
     *
     *  Bloom 金字塔（N = clamp(settings.bloom_mips, 2, MAX_BLOOM_MIPS)，各级 1/2 逐级减半）：
     *    extract:   scene → A0                    （亮部提取，阈值 ← settings）
     *    首级补模糊: A0 → B0 → A0                 （blur_h + blur_v）
     *    for i=1..N-1:  A[i] = blur_v(blur_h(down(A[i-1])))   （逐级降采样 + 模糊）
     *    B[N-1] = A[N-1]；for i=N-2..0: B[i] = A[i] + upsample_tent(B[i+1])（反向累加）
     *    composite: scene + B0 × strength →（可选 ACES）→（可选 gamma）→ 输出
     *
     *  ⚠️ 效果位与参数的唯一事实来源是 RendererSettings（get_renderer_settings()），
     *     本类不持有任何效果状态；DevGUI 直接读写 settings 即改即生效，不触发管线重装配。
     */
    class ID_API PostProcessPass : public RenderPass
    {
    public:
        /**
         *  @param output_fb  输出目标（无效 = ctx.viewport_fb / 默认屏幕）
         */
        PostProcessPass(FrameBufferID output_fb = FrameBufferID::invalid_id());
        virtual ~PostProcessPass() override = default;

    public:
        void set_output_fb(FrameBufferID fb) { m_output_fb = fb; }
        FrameBufferID get_output_fb() const { return m_output_fb; }

    public:
        // 声明依赖：读 SceneColor（输入场景 HDR）；写 ViewportTarget（ctx.viewport_fb，最终呈现目标）
        void setup(RenderPassBuilder& builder) override;

        virtual void execute(RenderContext& ctx) override;

    private:
        // 懒创建：postprocess shader + pipeline + bloom 金字塔 FBO 链（级数/窗口尺寸变化时重建）
        void ensure_resources(uint32_t window_w, uint32_t window_h);
        // 绑定目标 + 视口，绘制全屏三角形
        void render_fullscreen(PipelineID pipeline, FrameBufferID target, uint32_t w, uint32_t h);

        // 子步骤（尺寸随 mip 变化，均带参；函数体在 .cpp）
        void bloom_extract(RenderContext& ctx, FrameBufferID src);                  // scene → A0
        void bloom_blur(RenderContext& ctx, FrameBufferID src, FrameBufferID dst,   // 5-tap 高斯
            uint32_t w, uint32_t h, bool horizontal);
        void bloom_down(RenderContext& ctx, FrameBufferID src, FrameBufferID dst,   // 4-tap box 降采样
            uint32_t src_w, uint32_t src_h, uint32_t dst_w, uint32_t dst_h);
        void bloom_up(RenderContext& ctx, FrameBufferID src, FrameBufferID up_src,  // tent 9-tap 累加
            FrameBufferID dst, uint32_t dst_w, uint32_t dst_h, uint32_t up_w, uint32_t up_h);
        void composite(RenderContext& ctx, FrameBufferID src, FrameBufferID bloom_src, FrameBufferID dst);

    private:
        FrameBufferID  m_output_fb;                 // 输出目标（无效 = 默认屏幕）

        ShaderID       m_shader   = ShaderID::invalid_id();
        PipelineID     m_pipeline = PipelineID::invalid_id();

        static constexpr uint32_t MAX_BLOOM_MIPS = 6;   // 上限（settings.bloom_mips 可在 2~6 调）
        std::vector<FrameBufferID> m_bloom_a;   // A 链：extract / 各级降采样+模糊结果
        std::vector<FrameBufferID> m_bloom_b;   // B 链：反向累加结果（末级复用 A 链句柄）
        std::vector<uint32_t> m_mip_w;          // 各级宽度
        std::vector<uint32_t> m_mip_h;          // 各级高度
    };
} // namespace ID
