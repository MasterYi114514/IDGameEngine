#pragma once

#include <cstdint>
#include <sstream>
#include <string>
#include <functional>

#include "IDWindowCore.hpp"

#define ID_BIT(x) (static_cast<std::uint8_t>(1u << (x)))

/**
 * @brief 生成事件类所需的静态类型 / 名称 / 虚函数覆盖
 *
 * 用法：在派生事件类的 public 区域内调用一次。
 * @param type EventType 枚举值（如 KeyPressed）
 */
#define ID_EVENT_CLASS_TYPE(type)                                                   \
    static EventType      get_static_type()  { return EventType::type; }            \
    EventType             get_type() const override { return get_static_type(); }   \
    const char*           get_name() const override { return #type; }

/**
 * @brief 生成事件类别相关的静态方法 + 虚函数覆盖
 *
 * 用法：与 ID_EVENT_CLASS_TYPE 配合，放在派生事件类的 public 区域内。
 * @param expr EventCategory 表达式（如 EventCategory::Mouse | EventCategory::Input）
 */
#define ID_EVENT_CLASS_CATEGORY(expr)                                           \
    static EventCategory  get_static_category()  { return expr; }               \
    EventCategory         get_category() const override { return get_static_category(); }

namespace ID
{
    /*
    *   EventType 枚举定义了不同类型的事件，用于在事件系统中区分和处理各种事件
    */
    enum class EventType
    {
        None = 0,
        // 窗口事件
        WindowClose, WindowResize, WindowFocus, WindowLostFocus, WindowMoved,
        // 键盘事件
        KeyPressed, KeyReleased, KeyTyped,
        // 鼠标事件
        MouseButtonPressed, MouseButtonReleased, MouseMoved, MouseScrolled,
        // 应用事件
        AppTick, AppUpdate, AppRender
    };

    /*
    *   EventCategory 枚举定义了事件的类别，使用位掩码支持组合
    */
    enum class EventCategory : std::uint8_t
    {
        None        = 0,
        Application = ID_BIT(0),    // 应用事件
        Input       = ID_BIT(1),    // 输入事件（键盘、鼠标等）
        Keyboard    = ID_BIT(2),    // 键盘事件
        Mouse       = ID_BIT(3),    // 鼠标事件
        MouseButton = ID_BIT(4),    // 鼠标按键事件
    };

    // 允许用 | 组合多个类别
    constexpr EventCategory operator|(EventCategory a, EventCategory b)
    {
        return static_cast<EventCategory>(
            static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
    }

    /*
    *   Event 类是所有事件的基类，提供了事件类型的获取和处理状态的管理功能。
    */
    class IDWINDOW_API Event
    {
    public:
        virtual ~Event() = default;

        virtual EventType       get_type()     const = 0;
        virtual const char*     get_name()     const = 0;
        virtual EventCategory   get_category() const = 0;
        virtual std::string     to_string()    const = 0;

        /*
        *   检查事件是否已被处理。
        */
        bool is_handled() const { return m_handled; }

        /*
        *   设置事件的处理状态。
        *   @param handled 是否已被处理
        */
        void set_handled(bool handled) { m_handled = handled; }

    protected:
        bool m_handled = false;
    };
} // namespace ID
