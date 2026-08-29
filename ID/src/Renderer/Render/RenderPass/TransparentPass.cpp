#include "Renderer/Render/RenderPass/TransparentPass.hpp"
#include "Renderer/IDRCore.hpp"
#include "Renderer/Render/RenderGraph/RenderPassBuilder.hpp"
#include "Renderer/Mesh/Model.hpp"
#include "Renderer/Mesh/Mesh.hpp"
#include "Renderer/Render/RenderContext.hpp"

namespace ID
{
    TransparentPass::TransparentPass(FrameBufferID output_fb, const Vec3& ambient)
        : RenderPass("TransparentPass"), m_output_fb(output_fb), m_ambient(ambient) { }

    void TransparentPass::setup(RenderPassBuilder& builder)
    {
        // 顺序由 SceneColor 的 RAW/WAW/WAR 边保证：
        // 透明混合需要场景已有不透明内容（不透明+深度），该语义由资源边表达；
        // 深度取自当前绑定的场景 FBO（前向 = ForwardPass 写入，延迟 = LightingPass 从 G-Buffer blit）
        builder.read_writes(RGResource::SceneColor);
        builder.reads(RGResource::ShadowMap);         // 透明物体采样阴影（可选增强，悬空警告覆盖提示）
    }

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
