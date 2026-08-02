#include "Input/Input.hpp"

#ifdef IDWINDOW_USE_GLFW

#include <GLFW/glfw3.h>

namespace ID
{
    // 由 GLFWWindow 在构造时注入
    static GLFWwindow* s_native_window = nullptr;

    void Input::set_native_window(void* native_window)
    {
        s_native_window = static_cast<GLFWwindow*>(native_window);
    }

    bool Input::is_key_pressed(int key_code)
    {
        if(!s_native_window) return false;
        int state = glfwGetKey(s_native_window, key_code);
        return state == GLFW_PRESS || state == GLFW_REPEAT;
    }

    bool Input::is_mouse_button_pressed(int button)
    {
        if(!s_native_window) return false;
        int state = glfwGetMouseButton(s_native_window, button);
        return state == GLFW_PRESS;
    }

    std::pair<float, float> Input::get_mouse_position()
    {
        if(!s_native_window) return { 0.0f, 0.0f };
        double x, y;
        glfwGetCursorPos(s_native_window, &x, &y);
        return { static_cast<float>(x), static_cast<float>(y) };
    }

    float Input::get_mouse_x()
    {
        return get_mouse_position().first;
    }

    float Input::get_mouse_y()
    {
        return get_mouse_position().second;
    }
} // namespace ID

#endif      // #ifdef IDWINDOW_USE_GLFW