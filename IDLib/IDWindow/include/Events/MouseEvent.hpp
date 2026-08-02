#pragma once

#include "Event.hpp"

namespace ID
{
    /*
    *   MouseMovedEvent 类表示鼠标移动事件，继承自 Event。
    *   成员 m_mouse_x / m_mouse_y 记录鼠标在窗口内的坐标，默认均为 0.0f。
    */
    class MouseMovedEvent : public Event
    {
    public:
        MouseMovedEvent(float x, float y) : m_mouse_x(x), m_mouse_y(y) { }

        ID_EVENT_CLASS_TYPE(MouseMoved)
        ID_EVENT_CLASS_CATEGORY(EventCategory::Mouse | EventCategory::Input)

        float get_x() const { return m_mouse_x; }
        float get_y() const { return m_mouse_y; }

        std::string to_string() const override
        {
            std::stringstream ss;
            ss << get_name() << ": " << m_mouse_x << ", " << m_mouse_y;
            return ss.str();
        }

    private:
        float m_mouse_x = 0.0f;
        float m_mouse_y = 0.0f;
    };

    /*
    *   MouseScrolledEvent 类表示鼠标滚轮滚动事件，继承自 Event。
    *   成员 m_x_offset / m_y_offset 记录滚轮偏移量，默认均为 0.0f。
    */
    class MouseScrolledEvent : public Event
    {
    public:
        MouseScrolledEvent(float x_offset, float y_offset) : m_x_offset(x_offset), m_y_offset(y_offset) { }

        ID_EVENT_CLASS_TYPE(MouseScrolled)
        ID_EVENT_CLASS_CATEGORY(EventCategory::Mouse | EventCategory::Input)

        float get_x_offset() const { return m_x_offset; }
        float get_y_offset() const { return m_y_offset; }

        std::string to_string() const override
        {
            std::stringstream ss;
            ss << get_name() << ": " << m_x_offset << ", " << m_y_offset;
            return ss.str();
        }

    private:
        float m_x_offset = 0.0f;
        float m_y_offset = 0.0f;
    };

    /*
    *   MouseButtonEvent 鼠标按键事件基类。
    *   构造时须传入 GLFW 鼠标按键码（如 GLFW_MOUSE_BUTTON_LEFT）。
    *   子类 MouseButtonPressedEvent / MouseButtonReleasedEvent 通过 get_button() 获取按键码。
    */
    class MouseButtonEvent : public Event
    {
    public:
        int get_button() const { return m_button; }

        ID_EVENT_CLASS_CATEGORY(EventCategory::MouseButton | EventCategory::Input)

    protected:
        MouseButtonEvent(int button) : m_button(button) { }

        int m_button = -1;
    };

    /*
    *   MouseButtonPressedEvent 类表示鼠标按键按下事件，继承自 MouseButtonEvent。
    */
    class MouseButtonPressedEvent : public MouseButtonEvent
    {
    public:
        MouseButtonPressedEvent(int button) : MouseButtonEvent(button) { }

        ID_EVENT_CLASS_TYPE(MouseButtonPressed)

        std::string to_string() const override
        {
            std::stringstream ss;
            ss << get_name() << ": " << m_button;
            return ss.str();
        }
    };

    /*
    *   MouseButtonReleasedEvent 类表示鼠标按键释放事件，继承自 MouseButtonEvent。
    */
    class MouseButtonReleasedEvent : public MouseButtonEvent
    {
    public:
        MouseButtonReleasedEvent(int button) : MouseButtonEvent(button) { }

        std::string to_string() const override
        {
            std::stringstream ss;
            ss << get_name() << ": " << m_button;
            return ss.str();
        }

        ID_EVENT_CLASS_TYPE(MouseButtonReleased)
    };
} // namespace ID
