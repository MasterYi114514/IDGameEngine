#pragma once

#ifdef _ID_USE_IMPL

#define STB_IMAGE_IMPLEMENTATION
#include "Core/stb_image.h"

#include "Layer/Layer.hpp"
#include "Layer/CameraLayer.hpp"

#include "Renderer/Renderer.hpp"
#include "Renderer/Mesh/MeshFactory.hpp"
#include "Renderer/Material/MaterialLibrary.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/Component/MeshRendererComponent.hpp"
#include "Scene/Component/LightComponent.hpp"
#include "Log/Log.hpp"
#include "IDWindow.hpp"

#include <chrono>
#include <cmath>

namespace ID
{
    // =====================================================================
    //  RenderLayer — 渲染器压力测试 + 示例渲染
    //      on_attach: 加载资源 → 压力测试（计时）→ 创建演示物体
    //      on_update: 每帧调用 Renderer::render()
    // =====================================================================
    class RenderLayer : public Layer
    {
    public:
        RenderLayer(CameraLayer* camera_layer)
            : Layer("RenderLayer"), m_camera_layer(camera_layer) { }

        void on_attach() override
        {
            init_resources();
            run_stress_test();
            create_demo_objects();
        }

        void on_update(Timestep ts) override
        {
            if (!m_ready) return;

            const Camera& camera = m_camera_layer->get_camera();

            Renderer::clear_submissions();
            Renderer::submit_light(m_demo_light);
            Renderer::submit(m_demo_sphere_model, m_demo_sphere_world);
            Renderer::submit(m_demo_cuboid_model, m_demo_cuboid_world);
            Renderer::render(camera, nullptr, m_window_width, m_window_height, ts.get_seconds());

            // 每 60 帧打印一次统计
            m_frame_count++;
            if (m_frame_count % 60 == 0)
            {
                auto& s = Renderer::get_statistics();
                ID_TRACE("[RenderLayer] fps≈{:.0f}  draw={}  tri={}  lights={}  opaque={}  transparent={}",
                    1.0f / ts.get_seconds(), s.draw_calls, s.triangles, s.lights, s.opaque, s.transparent);
            }
        }

        void on_event(Event& event) override
        {
            if(event.get_type() == EventType::WindowResize)
            {
                event.set_handled(on_window_resize(static_cast<WindowResizeEvent&>(event)));
            }
        }

        bool on_window_resize(const WindowResizeEvent& event)
        {
            m_window_width  = event.get_width();
            m_window_height = event.get_height();
            return false;
        }

    private:
        using Clock = std::chrono::high_resolution_clock;

        CameraLayer*  m_camera_layer = nullptr;
        ShaderID      m_shader;
        TextureID     m_texture;
        MeshID        m_stress_mesh;      // 压力测试用的小立方体（共享）
        MeshID        m_sphere_mesh;
        MeshID        m_cuboid_mesh;

        // 演示物体手动提交数据
        Model  m_demo_sphere_model{ MeshID::invalid_id(), default_material_instance };
        Model  m_demo_cuboid_model{ MeshID::invalid_id(), default_material_instance };
        Mat4   m_demo_sphere_world = Math::get_identity_mat4();
        Mat4   m_demo_cuboid_world = Math::get_identity_mat4();
        Light  m_demo_light;

        bool          m_ready = false;
        int           m_frame_count = 0;
        uint32_t      m_window_width    = 1280;
        uint32_t      m_window_height   = 720;

        static double elapsed_ms(Clock::time_point start)
        {
            return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        }

        // =================================================================
        //  资源初始化
        // =================================================================
        void init_resources()
        {
            ID_INFO("[RenderLayer] 加载资源...");

            // 清屏色
            IDRCmd::set_clear_color(0.15f, 0.18f, 0.25f, 1.0f);

            // 加载 shader
            std::string vs = ShaderSourceLoader::load_shader_source(
                "../Assets/shader/geometry.vsl");
            std::string fs = ShaderSourceLoader::load_shader_source(
                "../Assets/shader/geometry.fsl");
            ShaderCreateInfo shader_info(vs, fs);
            m_shader = ShaderManager::create(shader_info);

            // 加载纹理（直接使用 stb_image，避免 TextureLoader 的兼容性问题）
            int tex_width, tex_height, tex_channels;
            unsigned char* tex_data = stbi_load("../Assets/texture/1.png",
                &tex_width, &tex_height, &tex_channels, 4);  // 强制 RGBA
            if (!tex_data)
            {
                ID_ERROR("[RenderLayer] 纹理加载失败: {}", stbi_failure_reason());
                return;
            }
            TextureCreateInfo tex_info(tex_width, tex_height, tex_data, TextureFormat::RGBA8);
            m_texture = TextureManager::create(tex_info);
            stbi_image_free(tex_data);

            // 注册共享材质
            Material* mat = MaterialLibrary::add(m_shader, "DefaultMat");
            mat->set_texture("texture_sampler", m_texture, 0);
            mat->set_param("u_color", Vec3(1.0f, 1.0f, 1.0f));

            // 预创建压力测试用小立方体（边长 0.5）
            m_stress_mesh = MeshFactory::create_cube(0.5f);

            ID_INFO("[RenderLayer] 资源加载完成");
        }

        // =================================================================
        //  压力测试：不同批次大小下的渲染耗时
        //  使用手动 submit 模式，排除场景遍历开销，纯测渲染管线
        // =================================================================
        void run_stress_test()
        {
            ID_INFO("========== Renderer 压力测试开始 ==========");

#ifdef _ID_DEBUG
            ID_WARN("当前为 Debug 构建，性能数据仅供参考");
#endif

            const Material* mat = MaterialLibrary::get("DefaultMat");
            if (!mat || !m_stress_mesh.is_valid())
            {
                ID_ERROR("[RenderLayer] 压力测试前提条件不满足，跳过");
                return;
            }

            MaterialInstance mat_inst(*mat);

            // 测试不同的 batch 数量
            int batch_sizes[] = { 10, 50, 100, 500, 1000, 2000, 5000 };
            constexpr int warmup_frames = 5;
            constexpr int measure_frames = 20;

            // 为最大 batch 预生成 Model + transform
            const int max_n = batch_sizes[sizeof(batch_sizes) / sizeof(batch_sizes[0]) - 1];
            std::vector<Model> models;
            std::vector<Mat4> transforms;
            models.reserve(max_n);
            transforms.reserve(max_n);

            // 按 3D 网格排列：cube_root^3 ≈ max_n
            int grid_dim = static_cast<int>(std::ceil(std::cbrt(static_cast<float>(max_n))));
            int idx = 0;
            for (int x = 0; x < grid_dim && idx < max_n; ++x)
            {
                for (int y = 0; y < grid_dim && idx < max_n; ++y)
                {
                    for (int z = 0; z < grid_dim && idx < max_n; ++z)
                    {
                        Model m(m_stress_mesh, mat_inst);
                        Mat4 world = Math::get_translation(Vec3(
                            (x - grid_dim / 2) * 2.0f,
                            (y - grid_dim / 2) * 2.0f,
                            (z - grid_dim / 2) * 2.0f));
                        models.push_back(m);
                        transforms.push_back(world);
                        ++idx;
                    }
                }
            }

            const Camera& camera = m_camera_layer->get_camera();

            ID_INFO("{:<6}  {:>10}  {:>10}  {:>10}", "N", "total(ms)", "avg(ms)", "draw/tri");
            ID_INFO("---------------------------------------------");

            for (int n : batch_sizes)
            {
                if (n > static_cast<int>(models.size())) break;

                // 预热
                for (int f = 0; f < warmup_frames; ++f)
                {
                    Renderer::clear_submissions();
                    for (int i = 0; i < n; ++i)
                        Renderer::submit(models[i], transforms[i]);
                    Renderer::render(camera);
                }

                // 正式测量
                double total_ms = 0.0;
                for (int f = 0; f < measure_frames; ++f)
                {
                    Renderer::clear_submissions();
                    for (int i = 0; i < n; ++i)
                        Renderer::submit(models[i], transforms[i]);

                    auto t0 = Clock::now();
                    Renderer::render(camera);
                    total_ms += elapsed_ms(t0);
                }

                double avg_ms = total_ms / measure_frames;
                auto& stats = Renderer::get_statistics();

                ID_INFO("{:<6}  {:>10.3f}  {:>10.3f}  {:>10} / {}",
                    n, total_ms, avg_ms, stats.draw_calls, stats.triangles);
            }

            // 清理压力测试残留
            Renderer::clear_submissions();

            ID_INFO("========== Renderer 压力测试结束 ==========");
        }

        // =================================================================
        //  演示物体：球体(r=6) + 长方体(6×4×2)，不重叠摆放
        // =================================================================
        void create_demo_objects()
        {
            ID_INFO("[RenderLayer] 创建演示物体...");

            const Material* mat = MaterialLibrary::get("DefaultMat");
            if (!mat)
            {
                ID_ERROR("[RenderLayer] DefaultMat 不存在");
                return;
            }

            MaterialInstance mat_inst(*mat);
            Scene& scene = SceneManager::get_current_scene();

            // 球体：半径 6，放在原点左侧
            m_sphere_mesh = MeshFactory::create_sphere(6.0f, 48, 24);
            ID_INFO("[RenderLayer] Sphere MeshID={} valid={}",
                m_sphere_mesh.get_id(), m_sphere_mesh.is_valid());
            m_demo_sphere_model = Model(m_sphere_mesh, mat_inst);
            m_demo_sphere_model.get_material().set_param(
                "u_color", Vec3(0.2f, 0.6f, 0.9f));
            m_demo_sphere_world = Math::get_translation(Vec3(-9.0f, 0.0f, -8.0f));

            // 长方体：6×4×2，放在球体右边不重叠
            // 球体 x=-9, r=6 → 占据 x∈[-15, -3]
            // 长方体放 x=6，半宽 3 → 占据 x∈[3, 9]，间距 = 6
            m_cuboid_mesh = MeshFactory::create_cuboid(6.0f, 4.0f, 2.0f);
            ID_INFO("[RenderLayer] Cuboid MeshID={} valid={}",
                m_cuboid_mesh.get_id(), m_cuboid_mesh.is_valid());
            m_demo_cuboid_model = Model(m_cuboid_mesh, mat_inst);
            m_demo_cuboid_model.get_material().set_param(
                "u_color", Vec3(0.9f, 0.4f, 0.2f));
            m_demo_cuboid_world = Math::get_translation(Vec3(6.0f, 0.0f, -8.0f));

            // 方向光
            m_demo_light.type = LightType::Directional;
            Vec3 ldir(-0.4f, -1.0f, -0.3f);
            ldir.normalize();
            m_demo_light.drop.direction = ldir;
            m_demo_light.intensity = 1.5f;

            m_ready = true;
            ID_INFO("[RenderLayer] 演示物体创建完成");
        }
    };

} // namespace ID

#endif
