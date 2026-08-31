#include "Renderer/Render/RenderPass/ForwardPass.hpp"
#include "Renderer/IDRCore.hpp"
#include "Renderer/Render/RenderGraph/RenderPassBuilder.hpp"
#include "Renderer/Mesh/Model.hpp"
#include "Renderer/Mesh/Mesh.hpp"
#include "Renderer/Render/RenderContext.hpp"

#include "Log/Log.hpp"

namespace ID
{
    ForwardPass::ForwardPass(FrameBufferID output_fb, const Vec3& ambient)
        : RenderPass("ForwardPass"), m_output_fb(output_fb), m_ambient(ambient) { }

    void ForwardPass::setup(RenderPassBuilder& builder)
    {
        builder.reads(RGResourceName::ShadowMap);    // 采样阴影（无 ShadowPass 时悬空警告为预期第二道保险）
        builder.writes(RGResourceName::SceneColor);
    }

    void ForwardPass::execute(RenderContext& ctx)
    {
        FrameBufferID target_fb = m_output_fb.is_valid() ? m_output_fb : (m_use_scene_fb ? ctx.scene_fb : FrameBufferID::invalid_id());

        // 如果 fb_id 无效，则沿用当前绑定的目标
        if(target_fb.is_valid())
        {
            IDRCmd::bind_framebuffer(target_fb);
        }

        if(ctx.window_width != 0 && ctx.window_height != 0)
        {
            // ForwardPass 使用整个窗口大小为 viewport
            IDRCmd::set_viewport(0, 0, ctx.window_width, ctx.window_height);
        }

        // 清屏
        IDRCmd::clear();

        // 不透明批次
        for(const auto& entry : ctx.opaque_batches)
        {
            RenderPass::draw_batch(ctx, entry, m_ambient);
        }

        // 透明批次
        if(m_render_transparent)
        {
            for(const auto& entry : ctx.transparent_batches)
            {
                RenderPass::draw_batch(ctx, entry, m_ambient);
            }
        }
    }
} // namespace ID