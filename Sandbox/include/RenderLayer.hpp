#pragma once

#include "ID.hpp"
#include "TextureLoader.hpp"
#include "CameraLayer.hpp"

namespace ID
{
    class RenderLayer : public Layer
    {
    public:
        RenderLayer(CameraLayer* camera_layer) : Layer("RenderLayer"), m_camera_layer(camera_layer) {}

        void on_attach() override
        {
            init();

            // 创建 Texture
            TD td;
            if(!td.load("../Assets/texture/1.png"))
            {
                std::cerr << "Failed to load texture" << std::endl;
                exit(-1);
            }

            TextureCreateInfo create_info(td.width, td.height, td.data, TextureFormat::RGBA8);
            m_texture = TextureManager::create(create_info);

            // 创建 Shader
            const std::string vss = ShaderSourceLoader::load_shader_source("../Assets/shader/geometry.vsl");
            const std::string fss = ShaderSourceLoader::load_shader_source("../Assets/shader/geometry.fsl");
            ShaderCreateInfo shader_create_info(vss, fss);
            ShaderID shader_id = ShaderManager::create(shader_create_info);

            // 创建 Pipeline
            VertexBufferLayout layout = Geometry::get_layout();
            PipelineCreateInfo pipeline_create_info(shader_id, layout);
            m_pipeline = PipelineManager::create(pipeline_create_info);

            // 创建几何体
            m_geometry = Geometry::create_sphere(1.0f, 32, 16);
        }

        void on_update(Timestep ts) override
        {
            IDRCmd::clear();

            // 绑定纹理
            IDRCmd::bind_texture(m_texture, 0);
            IDRCmd::set_param(m_pipeline, "texture_sampler", 0);

            // 计算 MVP 矩阵
            Mat4 mvp = m_camera_layer->get_projection_matrix() * m_camera_layer->get_view_matrix() * ID::Math::get_identity_mat4();
            IDRCmd::set_param(m_pipeline, "MVP", mvp);

            // 绘制几何体
            IDRCmd::draw_indexed(m_pipeline, m_geometry->get_vb(), m_geometry->get_ib());

            // 解绑纹理
            IDRCmd::unbind_texture(0);
        }

    private:
        void init()
        {
            static bool initialized = false;
            if(initialized) return;

            initialized = true;
            IDRCmd::set_clear_color(0.2f, 0.3f, 0.4f, 1.0f);
            IDRCmd::set_viewport(0, 0, 1280, 720);
        }
    private:
        PipelineID      m_pipeline = PipelineID();
        TextureID       m_texture = TextureID();

        CameraLayer*    m_camera_layer = nullptr;
        Geometry*       m_geometry = nullptr;
    };
}