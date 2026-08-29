#include "Renderer/Render/RenderPass/PostProcessPass.hpp"
#include "Renderer/Render/FullscreenQuad.hpp"
#include "Renderer/IDRCore.hpp"
#include "Renderer/Render/RenderGraph/RenderPassBuilder.hpp"

#include "Log/Log.hpp"

namespace ID
{
    // 与 postprocess.fsl 中的 u_mode 约定一致
    namespace
    {
        constexpr int PP_MODE_EXTRACT   = 0;   // 亮部提取
        constexpr int PP_MODE_BLUR_H    = 1;   // 水平模糊
        constexpr int PP_MODE_BLUR_V    = 2;   // 垂直模糊
        constexpr int PP_MODE_COMPOSITE = 3;   // 合成（+ tone mapping / gamma）
    } // 匿名命名空间

    PostProcessPass::PostProcessPass(FrameBufferID output_fb, uint32_t effects,
        float bloom_threshold, float bloom_strength)
        : RenderPass("PostProcessPass"),
          m_output_fb(output_fb),
          m_effects(effects),
          m_bloom_threshold(bloom_threshold),
          m_bloom_strength(bloom_strength)
    { }

    void PostProcessPass::setup(RenderPassBuilder& builder)
    {
        builder.reads(RGResource::SceneColor);      // 输入场景 HDR
        builder.writes(RGResource::ViewportTarget); // 输出最终呈现目标
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

        // bloom 中间缓冲：1/4 分辨率 RGBA16F（★ 依赖 IDRenderer color_format 增强）
        if (m_effects & PostProcessEffect_Bloom)
        {
            uint32_t bw = std::max(window_w / 4, 1u);
            uint32_t bh = std::max(window_h / 4, 1u);

            if (!m_bloom_a.is_valid() || m_bloom_w != bw || m_bloom_h != bh)
            {
                if (m_bloom_a.is_valid()) FBManager::destroy(m_bloom_a);
                if (m_bloom_b.is_valid()) FBManager::destroy(m_bloom_b);

                // bloom 中间缓冲：1/4 分辨率 RGBA16F
                FrameBufferCreateInfo info_a(bw, bh, TextureFormat::RGBA16F);
                m_bloom_a = FBManager::create(info_a);

                FrameBufferCreateInfo info_b(bw, bh, TextureFormat::RGBA16F);
                m_bloom_b = FBManager::create(info_b);

                m_bloom_w = bw;
                m_bloom_h = bh;
                ID_TRACE("[PostProcessPass] bloom 中间缓冲创建 {}x{}", bw, bh);
            }
        }
    }

    // 子步骤各自在绘制后更新统计（render_fullscreen 本身不持有 ctx）
    void PostProcessPass::bloom_extract(RenderContext& ctx, FrameBufferID src)
    {
        IDRCmd::bind_framebuffer_color(src, 0, 0);          // 输入场景 HDR → slot 0
        IDRCmd::set_param(m_pipeline, "u_input", 0);
        IDRCmd::set_param(m_pipeline, "u_mode", PP_MODE_EXTRACT);
        IDRCmd::set_param(m_pipeline, "u_threshold", m_bloom_threshold);
        render_fullscreen(m_pipeline, m_bloom_a, m_bloom_w, m_bloom_h);
        ID_RS_INC_DRAW_CALLS(ctx);
    }

    void PostProcessPass::bloom_blur(RenderContext& ctx, FrameBufferID src, FrameBufferID dst,
        bool horizontal)
    {
        IDRCmd::bind_framebuffer_color(src, 0, 0);          // 输入 bloom 中间 → slot 0
        IDRCmd::set_param(m_pipeline, "u_input", 0);
        IDRCmd::set_param(m_pipeline, "u_mode", horizontal ? PP_MODE_BLUR_H : PP_MODE_BLUR_V);
        IDRCmd::set_param(m_pipeline, "u_texel_size",
            Vec2(1.0f / static_cast<float>(m_bloom_w), 1.0f / static_cast<float>(m_bloom_h)));
        render_fullscreen(m_pipeline, dst, m_bloom_w, m_bloom_h);
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
            IDRCmd::set_param(m_pipeline, "u_bloom_strength", m_bloom_strength);
        }
        else
        {
            IDRCmd::unbind_texture(1);
        }

        IDRCmd::set_param(m_pipeline, "u_mode", PP_MODE_COMPOSITE);
        IDRCmd::set_param(m_pipeline, "u_has_bloom", has_bloom ? 1 : 0);
        IDRCmd::set_param(m_pipeline, "u_tone_mapping",
            (m_effects & PostProcessEffect_ToneMapping) ? 1 : 0);
        IDRCmd::set_param(m_pipeline, "u_gamma",
            (m_effects & PostProcessEffect_Gamma) ? 1 : 0);

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

        if (m_effects & PostProcessEffect_Bloom)
        {
            if (!m_bloom_a.is_valid())
            {
                ID_WARN("[PostProcessPass] bloom 中间缓冲无效，跳过 bloom");
                composite(ctx, src, FrameBufferID::invalid_id(), dst);
                return;
            }
            bloom_extract(ctx, src);                     // src → bloom_a
            bloom_blur(ctx, m_bloom_a, m_bloom_b, true); // 水平模糊
            bloom_blur(ctx, m_bloom_b, m_bloom_a, false);// 垂直模糊
            composite(ctx, src, m_bloom_a, dst);         // src + bloom_a → dst
        }
        else
        {
            composite(ctx, src, FrameBufferID::invalid_id(), dst);
        }
    }
} // namespace ID
