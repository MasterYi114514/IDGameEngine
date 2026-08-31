#include "Renderer/Render/RenderPass/LightingPass.hpp"
#include "Renderer/IDRCore.hpp"
#include "Renderer/Render/RendererSettings.hpp"
#include "Renderer/Render/RenderGraph/RenderPassBuilder.hpp"
#include "Renderer/Render/FullscreenQuad.hpp"
#include "Renderer/Render/RenderContext.hpp"
#include "Renderer/Render/RenderPass/GBufferPass.hpp"

#include "Log/Log.hpp"

#include <cmath>

namespace ID
{
    LightingPass::LightingPass(const Vec3& ambient) : RenderPass("LightingPass"), m_ambient(ambient) { }

    void LightingPass::setup(RenderPassBuilder& builder)
    {
        // 硬依赖：没有几何数据光照无从谈起（GBufferPass 必须先执行）
        builder.requires_pass<GBufferPass>();
        builder.reads(RGResourceName::GBuffer);      // 采样 G-Buffer 三附件
        builder.reads(RGResourceName::ShadowMap);    // 采样阴影（无 ShadowPass 时悬空警告为预期第二道保险）
        builder.writes(RGResourceName::SceneColor);  // 输出 HDR 场景色（供 Skybox/Transparent/PostProcess 读改写）
    }

    void LightingPass::ensure_resources()
    {
        if (m_shader.is_valid() && m_pipeline.is_valid())
        {
            return;
        }

        if (!m_shader.is_valid())
        {
            std::string vs = ShaderSourceLoader::load_shader_source("../Assets/shader/deferred_lighting.vsl");
            std::string fs = ShaderSourceLoader::load_shader_source("../Assets/shader/deferred_lighting.fsl");
            m_shader = ::ShaderManager::create(ShaderCreateInfo(vs, fs));
            if (!m_shader.is_valid())
            {
                ID_ERROR("[LightingPass] deferred_lighting shader 加载失败（deferred_lighting.vsl / .fsl）");
            }
        }

        if (!m_pipeline.is_valid() && m_shader.is_valid())
        {
            // 全屏绘制管线：关闭深度测试/写入/混合，cull=None（覆盖整个屏幕）
            PipelineState state;
            state.depth_test  = false;
            state.depth_write = false;
            state.cull_mode   = CullMode::None;
            state.blend       = false;

            m_pipeline = PipelineManager::create(
                PipelineCreateInfo(m_shader, FullscreenQuad::layout(), state));
        }
    }

    void LightingPass::execute(RenderContext& ctx)
    {
        ensure_resources();
        if (!m_shader.is_valid() || !m_pipeline.is_valid())
        {
            return;
        }

        // ★ 先绑定光照管线：set_param 的 glUniform 作用于当前绑定的 program，
        //   必须确保当前 program = deferred_lighting program（与 GBufferPass 同款约束），
        //   否则 uniform 写入上一 Pass 残留的 program，光照 shader 全为默认值 → 纯黑
        IDRCmd::bind_pipeline(m_pipeline);

        if (!ctx.scene_fb.is_valid())
        {
            ID_WARN("[LightingPass] ctx.scene_fb 无效，跳过延迟光照");
            return;
        }
        IDRCmd::bind_framebuffer(ctx.scene_fb);

        if (ctx.window_width != 0 && ctx.window_height != 0)
        {
            IDRCmd::set_viewport(0, 0, ctx.window_width, ctx.window_height);
        }

        // 清残留颜色与深度（深度随后由 G-Buffer blit 覆盖）
        IDRCmd::clear(true, true);

        // ★ 深度搬运：G-Buffer 深度 → 场景 FBO，供 Skybox(LessEqual)/Transparent 深度测试
        if (ctx.gbuffer_fb.is_valid())
        {
            IDRCmd::blit_framebuffer_depth(ctx.gbuffer_fb, ctx.scene_fb,
                ctx.window_width, ctx.window_height);

            // ★ blit 实现末尾会解绑 framebuffer（GL_FRAMEBUFFER=0），必须重新绑定场景 FBO，
            //   否则后续全屏三角形会绘制到默认窗口 FBO，scene_fb 保持清屏后的黑色
            IDRCmd::bind_framebuffer(ctx.scene_fb);
        }
        else
        {
            ID_WARN("[LightingPass] ctx.gbuffer_fb 无效，跳过深度 blit（天空/透明深度测试可能异常）");
        }

        // 绑定 G-Buffer 三个附件 → slot 0/1/2（布局见 gbuffer.fsl 顶部注释）
        IDRCmd::bind_framebuffer_color(ctx.gbuffer_fb, 0, 0);
        IDRCmd::set_param(m_pipeline, "u_gbuffer_albedo", 0);
        IDRCmd::bind_framebuffer_color(ctx.gbuffer_fb, 1, 1);
        IDRCmd::set_param(m_pipeline, "u_gbuffer_pos", 1);
        IDRCmd::bind_framebuffer_color(ctx.gbuffer_fb, 2, 2);
        IDRCmd::set_param(m_pipeline, "u_gbuffer_normal", 2);

        // 阴影：槽位与 RenderPass::apply_shadow 对应，仅后移（raw 3 / cmp 4）
        if (ctx.shadow_enabled && ctx.shadow_fb.is_valid() && ctx.shadow_depth_array.is_valid()
            && ctx.shadow_sampler_cmp.is_valid() && ctx.shadow_ubo.is_valid())
        {
            // 同一张 array 纹理绑两个槽：raw（blocker search 读原始深度）+ cmp（硬件比较采样）
            IDRCmd::bind_texture(ctx.shadow_depth_array, 3);
            IDRCmd::bind_sampler(ctx.shadow_sampler_raw, 3);
            IDRCmd::bind_texture(ctx.shadow_depth_array, 4);
            IDRCmd::bind_sampler(ctx.shadow_sampler_cmp, 4);
            IDRCmd::set_param(m_pipeline, "u_shadow_map", 3);
            IDRCmd::set_param(m_pipeline, "u_shadow_map_cmp", 4);

            // ShadowBlock UBO（P9：与 RenderPass::apply_shadow 一致，一次 bind 替代 10+ 次逐元素 set_param）
            IDRCmd::bind_uniform_buffer(ctx.shadow_ubo, 0);
            // CSM 选层需要主相机 view（延迟路径无顶点着色器提供，勿漏！）
            IDRCmd::set_param(m_pipeline, "u_view", ctx.camera.get_view_matrix());
        }
        else if (ctx.shadow_ubo.is_valid())
        {
            // 无阴影帧：仍 bind UBO（enabled=0 块，由 ShadowPass 上传或 Renderer 注入禁用 UBO），
            // 防 binding 0 残留上帧 enabled=1 数据 → 关闭阴影后阴影残留
            IDRCmd::bind_uniform_buffer(ctx.shadow_ubo, 0);
            IDRCmd::set_param(m_pipeline, "u_view", ctx.camera.get_view_matrix());
        }

        // 相机 + 环境光
        IDRCmd::set_param(m_pipeline, "u_camera_pos", ctx.camera.get_pose().position);
        IDRCmd::set_param(m_pipeline, "u_ambient", m_ambient);

        // 光照模型 / PBR 全局参数（RendererSettings，与 RenderPass::set_frame_uniforms 一致）
        const RendererSettings& settings = get_renderer_settings();
        IDRCmd::set_param(m_pipeline, "u_lighting_model", static_cast<int>(settings.lighting_model));
        IDRCmd::set_param(m_pipeline, "u_metallic",       settings.metallic);
        IDRCmd::set_param(m_pipeline, "u_roughness",      settings.roughness);

        // 光源数组 uniform：逐元素 set_param（名字数组模式与 RenderPass.cpp 一致，上限 32）
        if (ctx.lights.size() > MAX_LIGHTS)
        {
            ID_WARN("LightingPass: 光源数量为 {}, 已超出上限 {}", ctx.lights.size(), MAX_LIGHTS);
        }
        uint32_t light_count = std::min(static_cast<uint32_t>(ctx.lights.size()), MAX_LIGHTS);
        IDRCmd::set_param(m_pipeline, "u_light_count", static_cast<int>(light_count));

        for (uint32_t i = 0; i < light_count; ++i)
        {
            const Light& light = *(ctx.lights[i].light);
            // 光色为 sRGB 空间用户输入：先 pow 线性化再乘 intensity（与 RenderPass::set_frame_uniforms 逐字一致）
            Vec3 color{
                std::pow(light.color[0], 2.2f) * light.intensity,
                std::pow(light.color[1], 2.2f) * light.intensity,
                std::pow(light.color[2], 2.2f) * light.intensity };

            const std::string dir_name  = "u_light_dirs[" + std::to_string(i) + "]";
            const std::string pos_name  = "u_light_positions[" + std::to_string(i) + "]";
            const std::string col_name  = "u_light_colors[" + std::to_string(i) + "]";

            if (light.type == LightType::Directional)
            {
                IDRCmd::set_param(m_pipeline, dir_name, light.drop.direction);
                IDRCmd::set_param(m_pipeline, pos_name, Vec3(0.0f, 0.0f, 0.0f));
            }
            else
            {
                IDRCmd::set_param(m_pipeline, dir_name, Vec3(0.0f, 0.0f, 0.0f));
                IDRCmd::set_param(m_pipeline, pos_name, light.drop.position);
            }
            IDRCmd::set_param(m_pipeline, col_name, color);
        }

        // 全屏三角形
        IDRCmd::draw_arrays(m_pipeline, FullscreenQuad::vertex_buffer());
        ID_RS_INC_DRAW_CALLS(ctx);
    }
} // namespace ID
