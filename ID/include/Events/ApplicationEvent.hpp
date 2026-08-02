#pragma once

#include <Events/Event.hpp>

namespace ID
{
    // =====================================================================
    //  AppTickEvent
    // =====================================================================
    class ID_API AppTickEvent : public Event
    {
    public:
        AppTickEvent() = default;

        ID_EVENT_CLASS_TYPE(AppTick)
        ID_EVENT_CLASS_CATEGORY(EventCategory::Application)

        std::string to_string() const override
        {
            std::stringstream ss;
            ss << get_name();
            return ss.str();
        }
    };

    // =====================================================================
    //  AppUpdateEvent
    // =====================================================================
    class ID_API AppUpdateEvent : public Event
    {
    public:
        AppUpdateEvent() = default;

        ID_EVENT_CLASS_TYPE(AppUpdate)
        ID_EVENT_CLASS_CATEGORY(EventCategory::Application)

        std::string to_string() const override
        {
            std::stringstream ss;
            ss << get_name();
            return ss.str();
        }
    };

    // =====================================================================
    //  AppRenderEvent
    // =====================================================================
    class ID_API AppRenderEvent : public Event
    {
    public:
        AppRenderEvent() = default;

        ID_EVENT_CLASS_TYPE(AppRender)
        ID_EVENT_CLASS_CATEGORY(EventCategory::Application)

        std::string to_string() const override
        {
            std::stringstream ss;
            ss << get_name();
            return ss.str();
        }
    };
} // namespace ID
