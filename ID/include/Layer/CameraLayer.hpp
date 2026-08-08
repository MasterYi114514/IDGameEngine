#pragma once

#ifdef _ID_USE_IMPL

#include "Layer/Layer.hpp"
#include "Layer/LayerStack.hpp"

#include "Renderer/Camera/Camera.hpp"
#include "Renderer/Camera/CameraController.hpp"
#include "Log/Log.hpp"

namespace ID
{
    /**
     *  ID 内部实现的 CameraLayer，可以在实际游戏中自己实现
     */
    class CameraLayer : public Layer
    {
    public:
        CameraLayer() : Layer("CameraLayer") {}

        void on_attach() override
        {
            ID_TRACE("[CameraLayer] on_attach() 开始...");
            m_camera = Camera();
            ID_TRACE("[CameraLayer] Camera 构造完成，设置位置...");
            m_camera.set_position(Pos3(0.0f, 0.0f, 5.0f));
            ID_TRACE("[CameraLayer] 准备创建 FreeLookCameraController...");
            m_camera_controller = new FreeLookCameraController(m_camera);
            ID_TRACE("[CameraLayer] on_attach() 完成");
        }

        void on_update(Timestep ts) override
        {
            m_camera.update();
            if (m_camera_controller) m_camera_controller->on_update(ts);
        }
    public:
        const Camera& get_camera() const { return m_camera; }
        const Mat4& get_view_matrix() const { return m_camera.get_view_matrix(); }
        const Mat4& get_projection_matrix() const { return m_camera.get_projection_matrix(); }
        
    private:
        Camera m_camera;
        CameraController* m_camera_controller = nullptr;
    };
}

#endif