#include "Window/Window.hpp"
#include "Log/Log.hpp"

#ifdef IDWINDOW_USE_GLFW
    #include "Platform/GLFW/GLFWWindow.hpp"

namespace ID
{
    std::unique_ptr<Window> Window::create(const WindowProps& props)
    {
        return std::make_unique<GLFWWindow>(props);
    }
} // namespace ID

#else
#   error "Window::create: 未定义窗口后端宏（请定义 IDWINDOW_USE_GLFW 或其他后端宏）"
#endif
