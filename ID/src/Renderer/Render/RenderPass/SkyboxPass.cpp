#include "Renderer/Render/RenderPass/SkyboxPass.hpp"
#include "Renderer/IDRCore.hpp"
#include "Renderer/Render/RenderGraph/RenderPassBuilder.hpp"
#include "Renderer/Render/RenderPass/ForwardPass.hpp"
#include "Renderer/Mesh/MeshFactory.hpp"
#include "Renderer/Mesh/Mesh.hpp"

#include "Log/Log.hpp"

namespace ID
{
    SkyboxPass::SkyboxPass(bool procedural, const std::string& cubemap_dir)
        : RenderPass("SkyboxPass"),
          m_use_cubemap(!procedural && !cubemap_dir.empty()),
          m_cubemap_dir(cubemap_dir)
    { }

    void SkyboxPass::setup(RenderPassBuilder& builder)
    {
        builder.requires_pass<ForwardPass>();        // 硬依赖：需要 Forward 先清屏+写深度，天空盒才能正确填背景
        builder.read_writes(RGResource::SceneColor);   // 深度 LessEqual 填充背景，不清屏
    }

    bool SkyboxPass::load_cubemap(const std::string& dir)
    {
        // ★ 依赖 IDRenderer cubemap 纹理增强：
        //   TextureCreateInfo 增加 is_cubemap + 六面像素数据；TextureManager::create 创建 GL_TEXTURE_CUBE_MAP
        //
        // 约定目录内六面文件命名：posx / negx / posy / negy / posz / negz（.png / .jpg）
        //
        // static const char* faces[6] = { "posx", "negx", "posy", "negy", "posz", "negz" };
        // 逐面用 stbi_load 读取（ID/include/Core/stb_image.h），
        // 组装成 TextureCreateInfo{ is_cubemap=true, faces_data[6] } 后 TextureManager::create()。
        //
        // 当前引擎未实现该增强，cubemap 模式不可用，退回程序化天空。
        ID_WARN("[SkyboxPass] cubemap 模式需要 IDRenderer cubemap 增强，当前退回程序化天空");
        m_use_cubemap = false;
        return false;
    }

    void SkyboxPass::ensure_resources()
    {
        if (m_shader.is_valid() && m_cube.is_valid())
        {
            return;
        }

        if (!m_shader.is_valid())
        {
            std::string vs = ShaderSourceLoader::load_shader_source("../Assets/shader/skybox.vsl");
            std::string fs = ShaderSourceLoader::load_shader_source("../Assets/shader/skybox.fsl");
            m_shader = ::ShaderManager::create(ShaderCreateInfo(vs, fs));
            if (!m_shader.is_valid())
            {
                ID_ERROR("[SkyboxPass] skybox shader 加载失败（skybox.vsl / skybox.fsl）");
            }
        }

        if (!m_cube.is_valid())
        {
            // 边长 2 → 顶点在 ±1，v_dir = aPos 即方向向量
            m_cube = MeshFactory::create_cube(2.0f);
        }

        if (!m_pipeline.is_valid() && m_shader.is_valid() && m_cube.is_valid())
        {
            // LessEqual + 不写深度：只填充背景像素（深度恒为 1.0）
            PipelineState state;
            state.depth_test  = true;
            state.depth_write = false;
            state.depth_func  = DepthFunc::LessEqual;
            state.cull_mode   = CullMode::None;
            state.blend       = false;

            m_pipeline = PipelineManager::create(
                PipelineCreateInfo(m_shader, MeshFactory::get_layout(), state));
        }

        // cubemap 模式资源
        if (m_use_cubemap && !m_cubemap.is_valid())
        {
            if (!load_cubemap(m_cubemap_dir))
            {
                m_use_cubemap = false;
            }
        }
    }

    void SkyboxPass::execute(RenderContext& ctx)
    {
        ensure_resources();
        if (!m_shader.is_valid() || !m_cube.is_valid() || !m_pipeline.is_valid())
        {
            return;
        }

        // 渲染到场景 FBO（与 ForwardPass 同一目标，保证顺序正确）
        FrameBufferID target = ctx.scene_fb;
        if (target.is_valid())
        {
            IDRCmd::bind_framebuffer(target);
        }

        if (ctx.window_width != 0 && ctx.window_height != 0)
        {
            IDRCmd::set_viewport(0, 0, ctx.window_width, ctx.window_height);
        }

        // 相机矩阵（skybox 在无限远处，view 只取旋转部分，shader 内 mat4(mat3(view))）
        IDRCmd::set_param(m_pipeline, "u_projection", ctx.camera.get_projection_matrix());
        IDRCmd::set_param(m_pipeline, "u_view", ctx.camera.get_view_matrix());

        const Mesh& mesh = MeshFactory::get_mesh(m_cube);

        if (m_use_cubemap && m_cubemap.is_valid())
        {
            IDRCmd::set_param(m_pipeline, "u_use_cubemap", 1);
            IDRCmd::bind_texture(m_cubemap, 0);
            IDRCmd::set_param(m_pipeline, "u_cubemap", 0);
        }
        else
        {
            IDRCmd::set_param(m_pipeline, "u_use_cubemap", 0);
            IDRCmd::set_param(m_pipeline, "u_top_color",     m_top_color);
            IDRCmd::set_param(m_pipeline, "u_horizon_color", m_horizon_color);
            IDRCmd::set_param(m_pipeline, "u_bottom_color",  m_bottom_color);
            IDRCmd::set_param(m_pipeline, "u_sun_dir",       m_sun_dir);
            IDRCmd::set_param(m_pipeline, "u_sun_intensity", m_sun_intensity);
        }

        IDRCmd::draw_indexed(m_pipeline, mesh.get_vb(), mesh.get_ib());

        ID_RS_INC_DRAW_CALLS(ctx);
        ID_RS_INC_TRIANGLES(ctx, mesh.get_index_count() / 3);
    }
} // namespace ID
