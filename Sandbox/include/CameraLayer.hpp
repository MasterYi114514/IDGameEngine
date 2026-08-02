#pragma once

#include "ID.hpp"
#include "IDMath.hpp"

namespace ID
{
    class CameraLayer : public Layer
    {
    public:
        CameraLayer() : Layer("CameraLayer") {}

        void on_attach() override
        {
            m_camera = Camera();
            m_camera.set_position(Pos3(0.0f, 0.0f, 5.0f));
            m_camera_controller = new FreeLookCameraController(m_camera);
        }

        void on_update(Timestep ts) override
        {
            m_camera.update();
            if (m_camera_controller) m_camera_controller->on_update(ts);
        }
    public:
        const Mat4& get_view_matrix() const { return m_camera.get_view_matrix(); }
        const Mat4& get_projection_matrix() const { return m_camera.get_projection_matrix(); }
        
    private:
        Camera m_camera;
        CameraController* m_camera_controller = nullptr;
    };
}