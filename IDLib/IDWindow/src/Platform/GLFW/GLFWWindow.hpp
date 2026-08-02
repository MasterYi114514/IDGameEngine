#pragma once

#ifdef IDWINDOW_USE_GLFW

#include "Window/Window.hpp"

class GLFWwindow;           // 前向声明

// underlying window
// 因为 GLFWwindow 与 GLFWWindow 很像，有此区分
using ULWindow = GLFWwindow;
using ULWindowPtr = GLFWwindow*;

namespace ID
{
    class GLFWWindow : public Window
    {
    public:
        /*
        *   初始化 GLFW 窗口与 OpenGL 上下文
        *   1. 构造函数调用后并不会自动设置上下文为当前窗口，需要在 WindowPool 里显示调用
        *   2. 构造函数调用后会自动设置回调函数，回调函数会将事件传递给 WindowPool
        *   3. 构造函数调用时会自动初始化 GLAD，确保 OpenGL 函数可用
        */
        GLFWWindow(const WindowProps& props);
        ~GLFWWindow() override;

        void on_update() override;

        void set_event_callback(const EventCallbackFn& callback) override 
            { m_data.event_callback = callback; }

        void* get_native_handle() const override { return m_native_window; }

        void make_current() override;

    private:

        /*
        *   设置 GLFW 回调函数，包括：
        *     - WindowSize Callback
        *     - WindowClose Callback
        *     - WindowFocus Callback
        *     - WindowPos Callback
        *     - Key Callback
        *     - MouseButton Callback
        *     - CursorPos Callback
        *     - Scroll Callback
        */
        void set_callback();

        struct WindowData
        {
            std::string     title;
            uint32_t        width  = 1280;
            uint32_t        height = 720;
            EventCallbackFn event_callback;
        };

        WindowData  m_data;
        ULWindowPtr m_native_window = nullptr;

    public:

        uint32_t get_width()  const override { return m_data.width; }
        uint32_t get_height() const override { return m_data.height; }

        // 允许外部访问 WindowData，以便在 GLFW 回调中使用
        WindowData& get_window_data() { return m_data; }
    };
} // namespace ID

#endif // #ifdef ID_WINDOW_USE_GLFW