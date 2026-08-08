#pragma once

#ifdef _ID_USE_IMPL

#define STB_IMAGE_IMPLEMENTATION
#include "Core/stb_image.h"

#include "Layer/Layer.hpp"
#include "Layer/CameraLayer.hpp"

#include "Renderer/Render/Renderer.hpp"
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
            
        }

        void on_update(Timestep ts) override
        {

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
    };

} // namespace ID

#endif
