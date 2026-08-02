#pragma once

#include "Event.hpp"

namespace ID
{
    class WindowResizeEvent : public Event
    {
    public:
        WindowResizeEvent(uint32_t width, uint32_t height)
            : m_width(width), m_height(height) { }

        ID_EVENT_CLASS_TYPE(WindowResize)
        ID_EVENT_CLASS_CATEGORY(EventCategory::Application)

        uint32_t get_width()  const { return m_width; }
        uint32_t get_height() const { return m_height; }

        std::string to_string() const override
        {
            std::stringstream ss;
            ss << get_name() << ": " << m_width << " x " << m_height;
            return ss.str();
        }

    private:
        uint32_t m_width  = 0;
        uint32_t m_height = 0;
    };

    class WindowCloseEvent : public Event
    {
    public:
        WindowCloseEvent() = default;

        ID_EVENT_CLASS_TYPE(WindowClose)
        ID_EVENT_CLASS_CATEGORY(EventCategory::Application)

        std::string to_string() const override
        {
            std::stringstream ss;
            ss << get_name();
            return ss.str();
        }
    };

    class WindowFocusEvent : public Event
    {
    public:
        WindowFocusEvent() = default;

        ID_EVENT_CLASS_TYPE(WindowFocus)
        ID_EVENT_CLASS_CATEGORY(EventCategory::Application)

        std::string to_string() const override
        {
            std::stringstream ss;
            ss << get_name();
            return ss.str();
        }
    };

    class WindowLostFocusEvent : public Event
    {
    public:
        WindowLostFocusEvent() = default;

        ID_EVENT_CLASS_TYPE(WindowLostFocus)
        ID_EVENT_CLASS_CATEGORY(EventCategory::Application)

        std::string to_string() const override
        {
            std::stringstream ss;
            ss << get_name();
            return ss.str();
        }
    };

    class WindowMovedEvent : public Event
    {
    public:
        WindowMovedEvent(int x, int y)
            : m_x(x), m_y(y) { }

        ID_EVENT_CLASS_TYPE(WindowMoved)
        ID_EVENT_CLASS_CATEGORY(EventCategory::Application)

        int get_x() const { return m_x; }
        int get_y() const { return m_y; }

        std::string to_string() const override
        {
            std::stringstream ss;
            ss << get_name() << ": " << m_x << ", " << m_y;
            return ss.str();
        }

    private:
        int m_x = 0;
        int m_y = 0;
    };
} // namespace ID
