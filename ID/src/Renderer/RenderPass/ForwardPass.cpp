#include "Renderer/RenderPass/ForwardPass.hpp"
#include "Renderer/IDRCore.hpp"
#include "Renderer/Mesh/Model.hpp"
#include "Renderer/Mesh/Mesh.hpp"
#include "Renderer/RenderPass/RenderPassContext.hpp"

#include "Log/Log.hpp"

namespace ID
{
    ForwardPass::ForwardPass(FrameBufferID output_fb, const Vec3& ambient)
        : RenderPass("ForwardPass"), m_output_fb(output_fb), m_ambient(ambient) { }

    void ForwardPass::execute(RenderContext& ctx)
    {
        // 如果 fb_id 无效，则表示沿用当前绑定的目标
        if(m_output_fb.is_valid())
        {
            IDRCmd::bind_framebuffer(m_output_fb);
        }

        if(ctx.window_width != 0 && ctx.window_height != 0)
        {
            // ForwardPass 使用整个窗口大小为 viewport
            IDRCmd::set_viewport(0, 0, ctx.window_width, ctx.window_height);
        }

        // 清屏
        IDRCmd::clear();

        // 不透明批次
        for(const SubmitEntry& entry : ctx.opaque_batches)
        {
            draw_batch(ctx, entry);
        }

        // 透明批次
        for(const SubmitEntry& entry : ctx.transparent_batches)
        {
            draw_batch(ctx, entry);
        }
    }

    void ForwardPass::draw_batch(RenderContext& ctx, const SubmitEntry& entry)
    {
        const Model& model = entry.model;
        if(!model.is_valid())
        {
            return;
        }

        const MaterialInstance& material = model.get_material();
        ShaderID shader = material.get_shader();

        material.apply();

        set_frame_uniforms(ctx, shader);
        set_object_uniforms(ctx, shader, entry);

        IDRCmd::draw_indexed(entry.pipeline, model.get_mesh().get_vb(), model.get_mesh().get_ib());

        ID_RS_INC_DRAW_CALLS(ctx);
        ID_RS_INC_TRIANGLES(ctx, model.get_mesh().get_index_count() / 3);
    }

    void ForwardPass::set_frame_uniforms(RenderContext& ctx, ShaderID shader)
    {
        const Camera& camera = ctx.camera;

        // 相机
        IDRCmd::set_param(shader, "u_view",         camera.get_view_matrix());
        IDRCmd::set_param(shader, "u_proj",         camera.get_projection_matrix());
        IDRCmd::set_param(shader, "u_camera_pos",   camera.get_pose().position);
        IDRCmd::set_param(shader, "u_ambient",      m_ambient);
        IDRCmd::set_param(shader, "u_time",         ctx.time);

        // 光源
        if(ctx.lights.size() > MAX_LIGHTS)
        {
            ID_WARN("ForwardPass: 光源数量为 {}, 已超出上限 {}", ctx.lights.size(), MAX_LIGHTS);
        }

        uint32_t light_count = std::min(static_cast<uint32_t>(ctx.lights.size()), MAX_LIGHTS);
        IDRCmd::set_param(shader, "u_light_count", static_cast<int>(light_count));

        // 数组 uniform： u_light_dirs[i] / u_light_positions[i] / u_light_colors[i]
        for(uint32_t i = 0; i < light_count; ++i)
        {
            const Light& light = ctx.lights[i];
            Vec3 color = light.color * light.intensity;

            std::string idx = "[" + std::to_string(i) + "]";
            
            if(light.type == LightType::Directional)
            {
                IDRCmd::set_param(shader, "u_light_dirs"        + idx, light.drop.direction);
                IDRCmd::set_param(shader, "u_light_positions"   + idx, Vec3(0.0f, 0.0f, 0.0f));
            }
            else
            {
                IDRCmd::set_param(shader, "u_light_dirs"        + idx, Vec3(0.0f, 0.0f, 0.0f));
                IDRCmd::set_param(shader, "u_light_positions"   + idx, light.drop.position);
            }
            IDRCmd::set_param(shader, "u_light_colors" + idx, color);
        }
    }

    void ForwardPass::set_object_uniforms(RenderContext& ctx, ShaderID shader, const SubmitEntry& entry)
    {
        const Mat4& model = entry.world_transform;

        Mat4 mvp = ctx.camera.get_projection_matrix() * ctx.camera.get_view_matrix() * model;
        IDRCmd::set_param(shader, "u_mvp",   mvp);
        IDRCmd::set_param(shader, "u_model", entry.world_transform);
    }
} // namespace ID