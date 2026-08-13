#pragma once

#include "ID.hpp"

using namespace ID;

/*
*   RenderLayer — 渲染驱动层
*
*   每帧渲染 SceneManager 的当前激活场景（切换场景后视口自动跟随）。
*/
class RenderLayer : public Layer
{
public:
    RenderLayer(CameraLayer* camera_layer)
        : Layer("RenderLayer"), m_camera_layer(camera_layer) { }

    void on_attach() override
    {
        Renderer::set_visual_pipeline(true, true, true);
    }

    void on_update(Timestep ts) override
    {
        // 始终渲染当前激活场景（切换场景后视口跟随）
        Scene* scene = &SceneManager::get_current_scene();
        const Camera& camera = m_camera_layer->get_camera();

        Renderer::render(camera, scene, m_window_width, m_window_height, ts.get_seconds());
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
    CameraLayer* m_camera_layer = nullptr;
    uint32_t m_window_width  = 1280;
    uint32_t m_window_height = 720;
};
