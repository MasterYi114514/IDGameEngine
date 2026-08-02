#include "ID.hpp"

#include "CameraLayer.hpp"
#include "RenderLayer.hpp"

class Sandbox : public ID::Application
{
public:
    Sandbox() : Application("Sandbox", 1280, 720)
    {
        ID::CameraLayer* camera_layer = new ID::CameraLayer();
        push_layer(camera_layer);

        ID::RenderLayer* render_layer = new ID::RenderLayer(camera_layer);
        push_layer(render_layer);
    }
};

ID::Application* ID::create_application()
{
    return new Sandbox();
}