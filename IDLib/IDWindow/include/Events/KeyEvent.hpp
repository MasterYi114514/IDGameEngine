#pragma once

#include "Event.hpp"
#include "Input/KeyCode.hpp"

namespace ID
{
    /*
    *   KeyEvent 类是所有键盘事件的基类，提供了获取按键代码的功能。
    *   构造时须传入有效 KeyCode，子类通过 get_key_code() 获取按键信息。
    */
    class IDWINDOW_API KeyEvent : public Event
    {
    public:
        KeyCode get_key_code() const { return m_key_code; }
        ID_EVENT_CLASS_CATEGORY(EventCategory::Keyboard | EventCategory::Input);

    protected:
        KeyEvent(KeyCode key_code) : m_key_code(key_code) { }

        KeyCode m_key_code;
    };

    /*
    *   KeyPressedEvent 类表示按键按下事件，继承自 KeyEvent。
    *   m_repeat_count 记录按键重复次数（0 表示首次按下，>=1 表示重复触发）。
    */
    class KeyPressedEvent : public KeyEvent
    {
    public:
        KeyPressedEvent(KeyCode key_code, int repeat_count) : KeyEvent(key_code), m_repeat_count(repeat_count) { }

        ID_EVENT_CLASS_TYPE(KeyPressed)

        int get_repeat_count() const { return m_repeat_count; }

        std::string to_string() const override
        {
            std::stringstream ss;
            ss << get_name() << ": " << static_cast<int>(m_key_code.get_value()) << " (" << m_repeat_count << " repeats)";
            return ss.str();
        }

    private:
        int m_repeat_count = 0;
    };

    /*
    *   KeyReleasedEvent 类表示按键释放事件，继承自 KeyEvent。
    */
    class KeyReleasedEvent : public KeyEvent
    {
    public:
        KeyReleasedEvent(KeyCode key_code) : KeyEvent(key_code) { }

        ID_EVENT_CLASS_TYPE(KeyReleased)

        std::string to_string() const override
        {
            std::stringstream ss;
            ss << get_name() << ": " << static_cast<int>(m_key_code.get_value());
            return ss.str();
        }
    };

    // /*
    // *   KeyTypedEvent 类表示按键输入事件，继承自 KeyEvent。
    // */
    // class KeyTypedEvent : public KeyEvent
    // {
    // public:
    //     KeyTypedEvent(std::uint32_t codepoint) : m_codepoint(codepoint) { }
    //
    //     ID_EVENT_CLASS_TYPE(KeyTyped)
    //
    //     std::string to_string() const override
    //     {
    //         std::stringstream ss;
    //         ss << get_name() << ": " << m_codepoint;
    //         return ss.str();
    //     }
    //
    // private:
    //     // 码点：在 Unicode 中表示字符的唯一整数值
    //     std::uint32_t m_codepoint = 0;
    // };
} // namespace ID
