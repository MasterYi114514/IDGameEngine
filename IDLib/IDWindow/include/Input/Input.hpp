#pragma once

#include <utility>

#include "IDWindowCore.hpp"

namespace ID
{
    /*
    *   Input  — 纯静态输入轮询类
    *
    *       所有方法均为静态，无需实例化。
    *       原生窗口句柄由 WindowPool 内部注入，外部不可见。
    */
    class IDWINDOW_API Input
    {
    private:
        Input() = delete;
        ~Input() = delete;

    public:
        /*
        *   检查指定键是否被按下（支持按住重复）。
        *   @param key_code 键码，可使用 KeyCodes 命名空间中的常量
        *   @return 按下返回 true
        */
        static bool is_key_pressed(int key_code);

        /*
        *   检查指定鼠标按键是否被按下。
        *   @param button 鼠标按键码（与 GLFW 的 GLFW_MOUSE_BUTTON_* 值一致）
        *   @return 按下返回 true
        */
        static bool is_mouse_button_pressed(int button);

        /*
        *   获取鼠标在窗口内的坐标。
        *   @return {x, y} 坐标对
        */
        static std::pair<float, float> get_mouse_position();
        static float get_mouse_x();
        static float get_mouse_y();

    public:
        /*
        *   注入原生窗口句柄。由 WindowPool 在 set_current 时自动调用，
        *   外部代码不应直接使用。
        *   @param native_window 平台原生窗口指针（GLFW 下为 GLFWwindow*）
        */
        static void set_native_window(void* native_window);
    };
} // namespace ID
