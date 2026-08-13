#include "Renderer/IDRCore.hpp"
#include "Renderer/Render/RenderPass/RenderPass.hpp"
#include "Renderer/Mesh/Model.hpp"
#include "Renderer/Mesh/Mesh.hpp"
#include "Renderer/Render/RenderContext.hpp"

#include "Log/Log.hpp"

namespace
{
    const char* light_dir_name[8] = 
    {
        "u_light_dirs[0]", "u_light_dirs[1]", "u_light_dirs[2]", "u_light_dirs[3]",
        "u_light_dirs[4]", "u_light_dirs[5]", "u_light_dirs[6]", "u_light_dirs[7]"
    };

    const char* light_position_name[8] =
    {
        "u_light_positions[0]", "u_light_positions[1]", "u_light_positions[2]", "u_light_positions[3]",
        "u_light_positions[4]", "u_light_positions[5]", "u_light_positions[6]", "u_light_positions[7]"
    };

    const char* light_color_name[8] =
    {
        "u_light_colors[0]", "u_light_colors[1]", "u_light_colors[2]", "u_light_colors[3]",
        "u_light_colors[4]", "u_light_colors[5]", "u_light_colors[6]", "u_light_colors[7]"
    };
} // 匿名命名空间

namespace ID
{
    void RenderPass::draw_batch(RenderContext& ctx, const ModelSE& entry, Vec3 ambient)
    {
        const MaterialInstance& material = entry.material ? *entry.material : MaterialInstance(nullptr);
        ShaderID shader = material.get_shader();

        material.apply();

        set_frame_uniforms(ctx, shader, ambient);
        apply_shadow(ctx, shader);
        set_object_uniforms(ctx, shader, entry);

        auto& mesh = MeshFactory::get_mesh(entry.mesh);
        IDRCmd::draw_indexed(entry.pipeline, mesh.get_vb(), mesh.get_ib());

        ID_RS_INC_DRAW_CALLS(ctx);
        ID_RS_INC_TRIANGLES(ctx, mesh.get_index_count() / 3);
    }

    void RenderPass::set_frame_uniforms(RenderContext& ctx, ShaderID shader, Vec3 ambient)
    {
        const Camera& camera = ctx.camera;

        // 相机
        IDRCmd::set_param(shader, "u_view",         camera.get_view_matrix());
        IDRCmd::set_param(shader, "u_proj",         camera.get_projection_matrix());
        IDRCmd::set_param(shader, "u_camera_pos",   camera.get_pose().position);
        IDRCmd::set_param(shader, "u_ambient",      ambient);
        IDRCmd::set_param(shader, "u_time",         ctx.time);

        // 光源
        static constexpr uint32_t MAX_LIGHTS = 8;
        if(ctx.lights.size() > MAX_LIGHTS)
        {
            ID_WARN("ForwardPass: 光源数量为 {}, 已超出上限 {}", ctx.lights.size(), MAX_LIGHTS);
        }

        uint32_t light_count = std::min(static_cast<uint32_t>(ctx.lights.size()), MAX_LIGHTS);
        IDRCmd::set_param(shader, "u_light_count", static_cast<int>(light_count));

        // 数组 uniform： u_light_dirs[i] / u_light_positions[i] / u_light_colors[i]
        for(uint32_t i = 0; i < light_count; ++i)
        {
            const Light& light = *(ctx.lights[i].light);
            Vec3 color = light.color * light.intensity;

            
            if(light.type == LightType::Directional)
            {
                IDRCmd::set_param(shader, light_dir_name[i], light.drop.direction);
                IDRCmd::set_param(shader, light_position_name[i], Vec3(0.0f, 0.0f, 0.0f));
            }
            else
            {
                IDRCmd::set_param(shader, light_dir_name[i], Vec3(0.0f, 0.0f, 0.0f));
                IDRCmd::set_param(shader, light_position_name[i], light.drop.position);
            }
            IDRCmd::set_param(shader, light_color_name[i], color);
        }
    }

    void RenderPass::set_object_uniforms(RenderContext& ctx, ShaderID shader, const ModelSE& entry)
    {
        const Mat4& model = entry.world_transform;

        Mat4 mvp = ctx.camera.get_projection_matrix() * ctx.camera.get_view_matrix() * model;
        IDRCmd::set_param(shader, "u_mvp",   mvp);
        IDRCmd::set_param(shader, "u_model", entry.world_transform);
    }

    void RenderPass::apply_shadow(RenderContext& ctx, ShaderID shader)
    {
        if(ctx.shadow_enabled && ctx.shadow_fb.is_valid())
        {
            IDRCmd::bind_framebuffer_depth(ctx.shadow_fb, 1);
            IDRCmd::set_param(shader, "u_shadow_enabled", 1);
            IDRCmd::set_param(shader, "u_shadow_map", 1);
            IDRCmd::set_param(shader, "u_light_space_mvp", ctx.light_view_proj);
            IDRCmd::set_param(shader, "u_shadow_bias", ctx.shadow_bias);
            IDRCmd::set_param(shader, "u_shadow_pcf_radius", ctx.shadow_pcf_radius);
        }
        else
        {
            IDRCmd::set_param(shader, "u_shadow_enabled", 0);
        }
    }
} // namespace ID