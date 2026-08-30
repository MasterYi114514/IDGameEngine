#include "Renderer/Render/RendererSettings.hpp"

namespace ID
{
    RendererSettings& get_renderer_settings()
    {
        static RendererSettings instance;
        return instance;
    }
} // namespace ID
