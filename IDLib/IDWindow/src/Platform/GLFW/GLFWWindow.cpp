#ifdef IDWINDOW_USE_GLFW

#include "GLFWWindow.hpp"
#include "Events/Event.hpp"
#include "Events/KeyEvent.hpp"
#include "Events/MouseEvent.hpp"
#include "Events/WindowEvent.hpp"
#include "Input/KeyCode.hpp"
#include "Log/Log.hpp"

#define GLFW_INCLUDE_NONE

// IME 禁用（规避搜狗等输入法注入导致进程崩溃）：需先包含 windows.h
#ifdef _WIN32
    #include <windows.h>
    #include <imm.h>
#endif

#include <glad/glad.h>
#include <GLFW/glfw3.h>

// 暴露 Win32 原生句柄 API（glfwGetWin32Window）
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>


namespace
{
    bool glfw_init()
    {
        if(!glfwInit())
        {
            ID_WINDOW_ERROR("GLFWWindow: glfwInit 失败");
            return false;
        }

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 4);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        return true;
    }

    /*
        创建一个 GLFW 窗口，并返回其原生句柄
    */
    ULWindowPtr create_glfw_window(const ID::WindowProps& props)
    {
        ULWindowPtr native_window = glfwCreateWindow(
            static_cast<int>(props.width),
            static_cast<int>(props.height),
            props.title.c_str(), nullptr, nullptr
        );

        if(!native_window)
        {
            ID_WINDOW_ERROR("GLFWWindow: 窗口创建失败");
            glfwTerminate();
            return nullptr;
        }

        return native_window;
    }

    bool glad_init(ULWindowPtr window)
    {
        static bool glad_initialized = false;
        if(glad_initialized) return true;

        auto last_window = glfwGetCurrentContext();
        glfwMakeContextCurrent(window);

        if(!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
        {
            ID_WINDOW_ERROR("GLFWWindow: glad 初始化失败");
            glfwMakeContextCurrent(last_window);
            return false;
        }

        glfwMakeContextCurrent(last_window);
        glad_initialized = true;
        return true;
    }

} // 匿名命名空间

namespace ID
{
    GLFWWindow::GLFWWindow(const WindowProps& props) : m_data({ props.title, props.width, props.height })
    {
        // 初始化 GLFW
        if(!glfw_init()) return;

        // 创建 GLFW 窗口
        m_native_window = create_glfw_window(props);
        if(!m_native_window) return;

        if(!glad_init(m_native_window)) return;

        // 禁用 IME 输入上下文：搜狗等输入法（SogouPY.ime）注入本进程后
        // 会在窗口创建/输入处理阶段崩溃（0xc0000005），游戏窗口通常不需要系统 IME
        ImmAssociateContext(glfwGetWin32Window(m_native_window), nullptr);

        glfwSetWindowUserPointer(m_native_window, this);

        // 设置回调函数
        set_callback();

        ID_WINDOW_INFO("GLFWWindow 创建成功 ({} x {})", props.width, props.height);
    }

    GLFWWindow::~GLFWWindow()
    {
        if(m_native_window)
        {
            glfwDestroyWindow(m_native_window);
            m_native_window = nullptr;
        }
        glfwTerminate();
        ID_WINDOW_INFO("GLFW 窗口已销毁：原生窗口已销毁，OpenGL 上下文已释放（未显式删除的 GPU 资源由驱动回收），GLFW 已终止");
    }

    void GLFWWindow::set_callback()
    {
        // 设置 WindowResize Callback
        auto resize_callback = [](GLFWwindow* window, int width, int height)
        {
            GLFWWindow* IDWindow = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(window));
            WindowData& data = IDWindow->get_window_data();
            data.width  = static_cast<uint32_t>(width);
            data.height = static_cast<uint32_t>(height);

            WindowResizeEvent event(data.width, data.height);
            data.event_callback(event);
        };
        glfwSetWindowSizeCallback(m_native_window, resize_callback);

        // 设置 WindowClose Callback
        auto close_callback = [](GLFWwindow* window)
        {
            GLFWWindow* IDWindow = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(window));
            WindowData& data = IDWindow->get_window_data();
            WindowCloseEvent event;
            data.event_callback(event);
        };
        glfwSetWindowCloseCallback(m_native_window, close_callback);

        // 设置 WindowFocus Callback
        auto focus_callback = [](GLFWwindow* window, int focused)
        {
            GLFWWindow* IDWindow = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(window));
            WindowData& data = IDWindow->get_window_data();

            if(focused)
            {
                WindowFocusEvent event;
                data.event_callback(event);
            }
            else
            {
                WindowLostFocusEvent event;
                data.event_callback(event);
            }
        };
        glfwSetWindowFocusCallback(m_native_window, focus_callback);

        // 设置 WindowPos Callback
        auto pos_callback = [](GLFWwindow* window, int xpos, int ypos)
        {
            GLFWWindow* IDWindow = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(window));
            WindowData& data = IDWindow->get_window_data();
            WindowMovedEvent event(xpos, ypos);
            data.event_callback(event);
        };
        glfwSetWindowPosCallback(m_native_window, pos_callback);

        // 设置 Key Callback
        auto key_callback = [](GLFWwindow* window, int key, int scancode,  int action, int mods)
        {
            GLFWWindow* IDWindow = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(window));
            WindowData& data = IDWindow->get_window_data();

            switch(action)
            {
                case GLFW_PRESS:
                {
                    KeyPressedEvent event{ KeyCode(key), 0 };
                    data.event_callback(event);
                    break;
                }
                case GLFW_RELEASE:
                {
                    KeyReleasedEvent event{ KeyCode(key) };
                    data.event_callback(event);
                    break;
                }
                case GLFW_REPEAT:
                {
                    KeyPressedEvent event{ KeyCode(key), 1 };
                    data.event_callback(event);
                    break;
                }
            }
        };
        glfwSetKeyCallback(m_native_window, key_callback);

        // 设置 Mouse Button Callback
        auto mouse_button_callback = [](GLFWwindow* window, int button, int action, int mods)
        {
            GLFWWindow* IDWindow = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(window));
            WindowData& data = IDWindow->get_window_data();

            switch(action)
            {
                case GLFW_PRESS:
                {
                    MouseButtonPressedEvent event(button);
                    data.event_callback(event);
                    break;
                }
                case GLFW_RELEASE:
                {
                    MouseButtonReleasedEvent event(button);
                    data.event_callback(event);
                    break;
                }
            }
        };
        glfwSetMouseButtonCallback(m_native_window, mouse_button_callback);

        // 设置 Cursor Pos Callback
        auto cursor_pos_callback = [](GLFWwindow* window, double x_pos, double y_pos)
        {
            GLFWWindow* IDWindow = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(window));
            WindowData& data = IDWindow->get_window_data();
            MouseMovedEvent event(static_cast<float>(x_pos), static_cast<float>(y_pos));
            data.event_callback(event);
        };
        glfwSetCursorPosCallback(m_native_window, cursor_pos_callback);

        // 设置 Scroll Callback
        auto scroll_callback = [](GLFWwindow* window, double x_offset, double y_offset)
        {
            GLFWWindow* IDWindow = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(window));
            WindowData& data = IDWindow->get_window_data();
            MouseScrolledEvent event(static_cast<float>(x_offset), static_cast<float>(y_offset));
            data.event_callback(event);
        };
        glfwSetScrollCallback(m_native_window, scroll_callback);

        ID_WINDOW_TRACE("GLFWWindow: 回调函数设置完成");
    }

    void GLFWWindow::on_update()
    {
        glfwPollEvents();
        glfwSwapBuffers(m_native_window);
    }

    void GLFWWindow::make_current()
    {
        glfwMakeContextCurrent(m_native_window);
    }
 
} // namespace ID

#endif // #ifdef IDWINDOW_USE_GLFW