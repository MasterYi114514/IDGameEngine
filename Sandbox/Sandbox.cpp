#include "ID.hpp"
#include "Camera/CameraLayer.hpp"
#include "Scene/SceneLayer.hpp"
#include "Render/RenderLayer.hpp"
#include "FunctionGraphLayer.hpp"

class Sandbox : public ID::Application
{
public:
    Sandbox() : Application("Sandbox", 1280, 720)
    {
        m_camera_layer = new CameraLayer();
        push_layer(m_camera_layer);

        m_scene_layer = new SceneLayer();
        push_layer(m_scene_layer);

        m_render_layer = new ::RenderLayer(m_camera_layer, m_scene_layer);
        push_overlay(m_render_layer);
    }

private:
    FunctionGraphLayer* m_func_graph_layer = nullptr;
    CameraLayer* m_camera_layer = nullptr;
    SceneLayer* m_scene_layer = nullptr;
    ::RenderLayer* m_render_layer = nullptr;
};

ID::Application* ID::create_application()
{
    return new Sandbox();
}
