#include "Renderer/Render/RenderPass/TransparentPass.hpp"
#include "Renderer/IDRCore.hpp"
#include "Renderer/Mesh/Model.hpp"
#include "Renderer/Mesh/Mesh.hpp"
#include "Renderer/Render/RenderContext.hpp"

namespace ID
{
    TransparentPass::TransparentPass(FrameBufferID output_fb, const Vec3& ambient)
        : RenderPass("TransparentPass"), m_output_fb(output_fb), m_ambient(ambient) { }

    void TransparentPass::execute(RenderContext& ctx)
    {
        // 输出目标优先级：m_output_fb > ctx.scene_fb > 当前绑定目标
        FrameBufferID target = m_output_fb.is_valid() ? m_output_fb : ctx.scene_fb;
        if (target.is_valid())
        {
            IDRCmd::bind_framebuffer(target);
        }

        if (ctx.window_width != 0 && ctx.window_height != 0)
        {
            IDRCmd::set_viewport(0, 0, ctx.window_width, ctx.window_height);
        }

        // 透明批次（已远→近排序），blend 到已有 opaque + skybox 内容，不执行 clear
        for (const ModelSE& entry : ctx.transparent_batches)
        {
            RenderPass::draw_batch(ctx, entry, m_ambient);
        }
    }
} // namespace ID
