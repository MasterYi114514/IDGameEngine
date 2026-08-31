#include "Renderer/Render/RenderPass/PostProcessPass.hpp"
#include "Renderer/Render/FullscreenQuad.hpp"
#include "Renderer/IDRCore.hpp"
#include "Renderer/Render/RenderGraph/RenderPassBuilder.hpp"
#include "Renderer/Render/RendererSettings.hpp"

#include "Log/Log.hpp"

#include <algorithm>

namespace ID
{
    // 与 postprocess.fsl 中的 u_mode 约定一致
    namespace
    {
        constexpr int PP_MODE_EXTRACT    = 0;   // 亮部提取
        constexpr int PP_MODE_BLUR_H     = 1;   // 水平模糊
        constexpr int PP_MODE_BLUR_V     = 2;   // 垂直模糊
        constexpr int PP_MODE_COMPOSITE  = 3;   // 合成（+ tone mapping / gamma）
        constexpr int PP_MODE_DOWNSAMPLE = 4;   // 降采样（4-tap box）
        constexpr int PP_MODE_UPSAMPLE   = 5;   // 升采样累加（tent 9-tap）
    } // 匿名命名空间

    PostProcessPass::PostProcessPass(FrameBufferID output_fb)
        : RenderPass("PostProcessPass"), m_output_fb(output_fb)
    { }

    void PostProcessPass::setup(RenderPassBuilder& builder)
    {
        builder.reads(RGResourceName::SceneColor);      // 输入场景 HDR
        builder.writes(RGResourceName::ViewportTarget); // 输出最终呈现目标
    }

    void PostProcessPass::ensure_resources(uint32_t window_w, uint32_t window_h)
    {
        if (!m_shader.is_valid())
        {
            std::string vs = ShaderSourceLoader::load_shader_source("../Assets/shader/postprocess.vsl");
            std::string fs = ShaderSourceLoader::load_shader_source("../Assets/shader/postprocess.fsl");
            m_shader = ::ShaderManager::create(ShaderCreateInfo(vs, fs));
            if (!m_shader.is_valid())
            {
                ID_ERROR("[PostProcessPass] postprocess shader 加载失败（postprocess.vsl / .fsl）");
                return;
            }

            // 全屏绘制管线：关闭深度测试/写入/混合（覆盖整个屏幕）
            PipelineState state;
            state.depth_test  = false;
            state.depth_write = false;
            state.cull_mode   = CullMode::None;
            state.blend       = false;
            m_pipeline = PipelineManager::create(
                PipelineCreateInfo(m_shader, FullscreenQuad::layout(), state));
        }

        // bloom 金字塔：A/B 双链 RGBA16F（1/2 分辨率起逐级减半，各级保底 1；无深度附件）
        const RendererSettings& settings = get_renderer_settings();
        if (settings.post_bloom)
        {
            const uint32_t mip_count = std::clamp(settings.bloom_mips, 2u, MAX_BLOOM_MIPS);
            std::vector<uint32_t> mip_w(mip_count), mip_h(mip_count);
            mip_w[0] = std::max(window_w / 2, 1u);
            mip_h[0] = std::max(window_h / 2, 1u);
            for (uint32_t i = 1; i < mip_count; ++i)
            {
                mip_w[i] = std::max(mip_w[i - 1] / 2, 1u);   // 小窗口拖到极小时保底 1，防除零
                mip_h[i] = std::max(mip_h[i - 1] / 2, 1u);
            }

            // 级数或任一级尺寸变化 → 整链销毁重建（destroy-再-create，沿用既有模式）
            bool need_rebuild = (m_bloom_a.size() != mip_count);
            if (!need_rebuild)
            {
                for (uint32_t i = 0; i < mip_count; ++i)
                {
                    if (m_mip_w[i] != mip_w[i] || m_mip_h[i] != mip_h[i])
                    {
                        need_rebuild = true;
                        break;
                    }
                }
            }

            if (need_rebuild)
            {
                for (FrameBufferID& fb : m_bloom_a) { if (fb.is_valid()) FBManager::destroy(fb); }
                for (FrameBufferID& fb : m_bloom_b) { if (fb.is_valid()) FBManager::destroy(fb); }

                m_bloom_a.assign(mip_count, FrameBufferID::invalid_id());
                m_bloom_b.assign(mip_count, FrameBufferID::invalid_id());
                m_mip_w = mip_w;
                m_mip_h = mip_h;

                for (uint32_t i = 0; i < mip_count; ++i)
                {
                    // ★ 中间链 FBO 必须显式关深度附件（FrameBufferCreateInfo 默认 true）
                    FrameBufferCreateInfo info(mip_w[i], mip_h[i], TextureFormat::RGBA16F);
                    info.has_depth_attachment = false;
                    m_bloom_a[i] = FBManager::create(info);
                    m_bloom_b[i] = FBManager::create(info);
                }
                ID_TRACE("[PostProcessPass] bloom 金字塔创建 {} 级（{}x{} → {}x{}）",
                    mip_count, mip_w[0], mip_h[0], mip_w[mip_count - 1], mip_h[mip_count - 1]);
            }
        }
    }

    // 子步骤各自在绘制后更新统计（render_fullscreen 本身不持有 ctx）
    void PostProcessPass::bloom_extract(RenderContext& ctx, FrameBufferID src)
    {
        IDRCmd::bind_framebuffer_color(src, 0, 0);          // 输入场景 HDR → slot 0
        IDRCmd::set_param(m_pipeline, "u_input", 0);
        IDRCmd::set_param(m_pipeline, "u_mode", PP_MODE_EXTRACT);
        IDRCmd::set_param(m_pipeline, "u_threshold", get_renderer_settings().bloom_threshold);
        render_fullscreen(m_pipeline, m_bloom_a[0], m_mip_w[0], m_mip_h[0]);
        ID_RS_INC_DRAW_CALLS(ctx);
    }

    void PostProcessPass::bloom_blur(RenderContext& ctx, FrameBufferID src, FrameBufferID dst,
        uint32_t w, uint32_t h, bool horizontal)
    {
        IDRCmd::bind_framebuffer_color(src, 0, 0);          // 输入 bloom 中间 → slot 0
        IDRCmd::set_param(m_pipeline, "u_input", 0);
        IDRCmd::set_param(m_pipeline, "u_mode", horizontal ? PP_MODE_BLUR_H : PP_MODE_BLUR_V);
        IDRCmd::set_param(m_pipeline, "u_texel_size",
            Vec2(1.0f / static_cast<float>(w), 1.0f / static_cast<float>(h)));
        render_fullscreen(m_pipeline, dst, w, h);
        ID_RS_INC_DRAW_CALLS(ctx);
    }

    void PostProcessPass::bloom_down(RenderContext& ctx, FrameBufferID src, FrameBufferID dst,
        uint32_t src_w, uint32_t src_h, uint32_t dst_w, uint32_t dst_h)
    {
        IDRCmd::bind_framebuffer_color(src, 0, 0);          // 上一级 → slot 0
        IDRCmd::set_param(m_pipeline, "u_input", 0);
        IDRCmd::set_param(m_pipeline, "u_mode", PP_MODE_DOWNSAMPLE);
        // u_texel_size = 源（上一级）尺寸的倒数（shader 半像素偏移采样用）
        IDRCmd::set_param(m_pipeline, "u_texel_size",
            Vec2(1.0f / static_cast<float>(src_w), 1.0f / static_cast<float>(src_h)));
        render_fullscreen(m_pipeline, dst, dst_w, dst_h);
        ID_RS_INC_DRAW_CALLS(ctx);
    }

    void PostProcessPass::bloom_up(RenderContext& ctx, FrameBufferID src, FrameBufferID up_src,
        FrameBufferID dst, uint32_t dst_w, uint32_t dst_h, uint32_t up_w, uint32_t up_h)
    {
        IDRCmd::bind_framebuffer_color(src, 0, 0);          // 本级 Ai → slot 0
        IDRCmd::bind_framebuffer_color(up_src, 0, 1);       // 上级 B[i+1] → slot 1（与 composite 复用）
        IDRCmd::set_param(m_pipeline, "u_input", 0);
        IDRCmd::set_param(m_pipeline, "u_bloom", 1);
        IDRCmd::set_param(m_pipeline, "u_mode", PP_MODE_UPSAMPLE);
        // u_texel_size = 上级（采样源）尺寸的倒数；步长倍率由 shader 内 ×u_bloom_radius
        IDRCmd::set_param(m_pipeline, "u_texel_size",
            Vec2(1.0f / static_cast<float>(up_w), 1.0f / static_cast<float>(up_h)));
        render_fullscreen(m_pipeline, dst, dst_w, dst_h);
        ID_RS_INC_DRAW_CALLS(ctx);
    }

    void PostProcessPass::composite(RenderContext& ctx, FrameBufferID src,
        FrameBufferID bloom_src, FrameBufferID dst)
    {
        IDRCmd::bind_framebuffer_color(src, 0, 0);          // 输入场景 HDR → slot 0
        IDRCmd::set_param(m_pipeline, "u_input", 0);

        bool has_bloom = bloom_src.is_valid();
        if (has_bloom)
        {
            IDRCmd::bind_framebuffer_color(bloom_src, 0, 1);    // bloom 图 → slot 1
            IDRCmd::set_param(m_pipeline, "u_bloom", 1);
            IDRCmd::set_param(m_pipeline, "u_bloom_strength", get_renderer_settings().bloom_strength);
        }
        else
        {
            IDRCmd::unbind_texture(1);
        }

        const RendererSettings& settings = get_renderer_settings();
        IDRCmd::set_param(m_pipeline, "u_mode", PP_MODE_COMPOSITE);
        IDRCmd::set_param(m_pipeline, "u_has_bloom", has_bloom ? 1 : 0);
        IDRCmd::set_param(m_pipeline, "u_tone_mapping",
            settings.post_tone_mapping ? 1 : 0);
        IDRCmd::set_param(m_pipeline, "u_gamma",
            settings.post_gamma ? 1 : 0);

        render_fullscreen(m_pipeline, dst, ctx.window_width, ctx.window_height);
        ID_RS_INC_DRAW_CALLS(ctx);
    }

    void PostProcessPass::render_fullscreen(PipelineID pipeline, FrameBufferID target,
        uint32_t w, uint32_t h)
    {
        if (target.is_valid())
        {
            IDRCmd::bind_framebuffer(target);
        }
        else
        {
            IDRCmd::bind_default_framebuffer();
        }
        IDRCmd::set_viewport(0, 0, w, h);
        IDRCmd::draw_arrays(pipeline, FullscreenQuad::vertex_buffer());
    }

    void PostProcessPass::execute(RenderContext& ctx)
    {
        // 输入：ForwardPass 输出的场景 HDR FBO
        FrameBufferID src = ctx.scene_fb;
        if (!src.is_valid() || ctx.window_width == 0 || ctx.window_height == 0)
        {
            ID_TRACE("[PostProcessPass] 场景 FBO 无效，跳过后处理");
            return;
        }

        // 效果位与参数的唯一事实来源：RendererSettings（DevGUI 即改即生效，不重建管线）
        const RendererSettings& settings = get_renderer_settings();
        const uint32_t effects =
            (settings.post_bloom        ? PostProcessEffect_Bloom       : 0u) |
            (settings.post_tone_mapping ? PostProcessEffect_ToneMapping : 0u) |
            (settings.post_gamma        ? PostProcessEffect_Gamma       : 0u);

        ensure_resources(ctx.window_width, ctx.window_height);
        if (!m_shader.is_valid() || !m_pipeline.is_valid())
        {
            return;
        }

        FrameBufferID dst = m_output_fb;
        // 未显式指定输出目标时：优先输出到 ctx.viewport_fb（显示 FBO，供窗口/Viewport 面板使用），
        // 仍无效则输出默认屏幕
        if(!dst.is_valid() && ctx.viewport_fb.is_valid())
        {
            dst = ctx.viewport_fb;
        }

        if (effects & PostProcessEffect_Bloom)
        {
            const uint32_t mip_count = static_cast<uint32_t>(m_bloom_a.size());
            if (mip_count == 0)
            {
                ID_WARN("[PostProcessPass] bloom 金字塔无效，跳过 bloom");
                composite(ctx, src, FrameBufferID::invalid_id(), dst);
                return;
            }

            // 模糊/升采样步长倍率（每帧读取，DevGUI 即改即生效）
            IDRCmd::set_param(m_pipeline, "u_bloom_radius", settings.bloom_radius);

            // ⚠️ 解绑残留 sampler：GL sampler object 状态优先于纹理自身 wrap 参数，且为进程级持久。
            // ShadowPass 曾把 ClampToBorder+白border 的 shadow sampler 绑到 slot 1/2，若不解绑，
            // bloom 的 u_bloom(slot1) 越界 tap 会读到白色 border → 屏幕边缘一圈亮线。
            // 解绑后采样回退纹理自身的 ClampToEdge（FrameBuffer 附件默认）。
            IDRCmd::unbind_sampler(0);
            IDRCmd::unbind_sampler(1);

            // ① extract：scene → A0，首级补模糊 A0 → B0 → A0
            bloom_extract(ctx, src);
            bloom_blur(ctx, m_bloom_a[0], m_bloom_b[0], m_mip_w[0], m_mip_h[0], true);
            bloom_blur(ctx, m_bloom_b[0], m_bloom_a[0], m_mip_w[0], m_mip_h[0], false);

            // ② 逐级降采样 + 模糊：A[i] = blur_v(blur_h(down(A[i-1])))
            for (uint32_t i = 1; i < mip_count; ++i)
            {
                bloom_down(ctx, m_bloom_a[i - 1], m_bloom_a[i],
                    m_mip_w[i - 1], m_mip_h[i - 1], m_mip_w[i], m_mip_h[i]);
                bloom_blur(ctx, m_bloom_a[i], m_bloom_b[i], m_mip_w[i], m_mip_h[i], true);
                bloom_blur(ctx, m_bloom_b[i], m_bloom_a[i], m_mip_w[i], m_mip_h[i], false);
            }

            // ③ 反向累加：B[N-1] = A[N-1]（末级句柄复用，A[N-1] 不再被写）；
            //    B[i] = A[i] + tent(B[i+1])，i = N-2 .. 0
            m_bloom_b[mip_count - 1] = m_bloom_a[mip_count - 1];
            for (uint32_t i = mip_count - 1; i-- > 0; )
            {
                bloom_up(ctx, m_bloom_a[i], m_bloom_b[i + 1], m_bloom_b[i],
                    m_mip_w[i], m_mip_h[i], m_mip_w[i + 1], m_mip_h[i + 1]);
            }

            // ④ 合成：scene + B0 × strength → dst
            composite(ctx, src, m_bloom_b[0], dst);
        }
        else
        {
            composite(ctx, src, FrameBufferID::invalid_id(), dst);
        }
    }
} // namespace ID
