#include "ID.hpp"
#include "Camera/CameraLayer.hpp"
#include "Scene/SceneLayer.hpp"
#include "Render/RenderLayer.hpp"

class Sandbox : public ID::Application
{
public:
    Sandbox() : Application("Sandbox", 1280, 720)
    {
        // 创建 CameraLayer
        m_camera_layer = new CameraLayer();
        push_layer(m_camera_layer);

        // 创建 SceneLayer
        m_scene_layer = new SceneLayer();
        push_layer(m_scene_layer);

        // 创建 RenderLayer
        m_render_layer = new ::RenderLayer(m_camera_layer, m_scene_layer);
        push_overlay(m_render_layer);
    }

private:
    CameraLayer* m_camera_layer = nullptr;
    SceneLayer* m_scene_layer = nullptr;
    ::RenderLayer* m_render_layer = nullptr;
};

ID::Application* ID::create_application()
{
    return new Sandbox();
}
