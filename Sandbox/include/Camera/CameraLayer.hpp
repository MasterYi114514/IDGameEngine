#pragma once

#include "ID.hpp"

using namespace ID;

class CameraLayer : public Layer
{
public:
    CameraLayer() : Layer("CameraLayer"), m_controller(m_camera) { }

    void on_attach() override
    {
        m_camera.set_position(Pos3(0.0f, 0.0f, 5.0f));
    }

    void on_update(Timestep ts) override
    {
        m_controller.on_update(ts);
    }

    // ★ 新增：把事件转发给控制器
    void on_event(Event& event) override
    {
        m_controller.on_event(event);
    }

public:
    const Mat4& get_view_matrix() const { return m_camera.get_view_matrix(); }
    const Camera& get_camera() const { return m_camera; }
    Camera& get_camera() { return m_camera; }
    
private:
    Camera                      m_camera = Camera(CameraPose(), ProjectionParams());
    FreeLookCameraController    m_controller;
};