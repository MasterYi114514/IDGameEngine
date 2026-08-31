#include "Renderer/IDRCore.hpp"
#include "Renderer/Render/RenderPass/RenderPass.hpp"
#include "Renderer/Render/RendererSettings.hpp"
#include "Renderer/Mesh/Model.hpp"
#include "Renderer/Mesh/Mesh.hpp"
#include "Renderer/Render/RenderContext.hpp"

#include "Log/Log.hpp"

#include <cmath>

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

        // 每批绘制前解绑纹理单元 0：材质未绑定纹理时，避免采样到上一批残留的纹理
        // （GLSL sampler 默认绑 unit 0；残留会让"无纹理=纯黑"失效，显示上个物体的纹理）
        // 有纹理的材质会在 material.apply() 中重新绑定自己的纹理
        IDRCmd::unbind_texture(0);

        // 写入材质参数，包括父级默认值 + 局部覆盖
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
            // 光色为 sRGB 空间用户输入：先 pow 线性化再乘 intensity（shader 侧 pow 会把 intensity 一起 gamma 掉）
            Vec3 color{
                std::pow(light.color[0], 2.2f) * light.intensity,
                std::pow(light.color[1], 2.2f) * light.intensity,
                std::pow(light.color[2], 2.2f) * light.intensity };

            
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

        // 光照模型 / PBR 全局参数（RendererSettings，与 LightingPass 一致）
        const RendererSettings& settings = get_renderer_settings();
        IDRCmd::set_param(shader, "u_lighting_model", static_cast<int>(settings.lighting_model));
        IDRCmd::set_param(shader, "u_metallic",       settings.metallic);
        IDRCmd::set_param(shader, "u_roughness",      settings.roughness);
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
        if(ctx.shadow_enabled && ctx.shadow_fb.is_valid() && ctx.shadow_depth_array.is_valid()
            && ctx.shadow_sampler_cmp.is_valid() && ctx.shadow_ubo.is_valid())
        {
            // 同一张 array 纹理绑两个槽：raw（blocker search 读原始深度）+ cmp（硬件比较采样）
            IDRCmd::bind_texture(ctx.shadow_depth_array, 1);
            IDRCmd::bind_sampler(ctx.shadow_sampler_raw, 1);
            IDRCmd::bind_texture(ctx.shadow_depth_array, 2);
            IDRCmd::bind_sampler(ctx.shadow_sampler_cmp, 2);
            IDRCmd::set_param(shader, "u_shadow_map", 1);
            IDRCmd::set_param(shader, "u_shadow_map_cmp", 2);

            // ShadowBlock UBO（P9：ShadowPass 每帧一次 glBufferSubData 上传全部阴影数据，
            // 替代 P8 的 10+ 次逐元素 set_param；u_view 已由 set_frame_uniforms 设置）
            IDRCmd::bind_uniform_buffer(ctx.shadow_ubo, 0);
        }
        else if(ctx.shadow_ubo.is_valid())
        {
            // 无阴影帧：仍 bind UBO（enabled=0 块，由 ShadowPass 上传或 Renderer 注入禁用 UBO），
            // 防 binding 0 残留上帧 enabled=1 数据 → 关闭阴影后阴影残留
            IDRCmd::bind_uniform_buffer(ctx.shadow_ubo, 0);
        }
    }
} // namespace ID