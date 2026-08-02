#pragma once

#include <cstdint>
#include <memory>
#include <functional>
#include <string>

#include "IDWindowCore.hpp"
#include "Window/WindowProps.hpp"

namespace ID
{
    class Event;

    /*
    *   Window  — 窗口抽象接口（内部实现，外界不可见）
    *
    *       所有平台相关的窗口实现（GLFW、Win32、X11 等）均继承此类。
    *       由 WindowPool 通过工厂创建并管理，外部代码不直接接触。
    */
    class Window
    {
    public:
        using EventCallbackFn = std::function<void(Event&)>;

        virtual ~Window() = default;

        virtual void on_update() = 0;

        virtual uint32_t get_width()  const = 0;
        virtual uint32_t get_height() const = 0;

        virtual void set_event_callback(const EventCallbackFn& callback) = 0;
        virtual void* get_native_handle() const = 0;

        /*
        *   激活此窗口的渲染上下文（OpenGL/Vulkan 等）。
        *   由 WindowPool::set_current 自动调用。
        */
        virtual void make_current() = 0;

        static std::unique_ptr<Window> create(const WindowProps& props);
    };
} // namespace ID
