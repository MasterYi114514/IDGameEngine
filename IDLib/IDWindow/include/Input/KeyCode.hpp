#pragma once

#include <cstdint>
#include <unordered_map>

#include "IDWindowCore.hpp"

namespace ID
{
    using KeyCodeType = std::int16_t;

    /*
    *   KeyCode 类用于表示键盘按键的键码。它封装了一个底层的键码值，并提供了相关的操作和状态管理功能。
    *     - 该类禁止默认构造函数，必须通过显式构造函数传入键码值进行初始化。
    *     - 提供了相等比较操作符，用于比较两个 KeyCode 对象或 KeyCode 对象与底层键码值的相等性。
    *     - 提供了判断键位是否为 ctrl、shift、alt 或 super 键的成员函数。
    *     - 设计上，确保该类的传递和使用是轻量级的，与 KeyCodeType 类型的整数值进行交互时不会产生额外的开销。
    *     - 如果使用了 ImGui 库，还提供将 KeyCode 转换为 ImGuiKey 的功能。
    *
    *   注：
    *   1. 在调用 KeyCode 类的任何功能之前，必须先调用 `::ID::KeyCode::init()` 函数进行键码状态映射表的初始化。
    */
    class IDWINDOW_API KeyCode
    {
        using UnderlyingType = KeyCodeType;
        using StateMap = std::unordered_map<UnderlyingType, bool>;      // 键码状态映射表
    public:
        KeyCode() = delete;         // 禁止默认构造函数
        constexpr explicit KeyCode(UnderlyingType value) : m_value(value) {}
        constexpr UnderlyingType get_value() const { return m_value; }
        constexpr operator UnderlyingType() const { return m_value; }

        /*
            该函数用于初始化键码状态映射表，确保在使用 KeyCode 类之前调用该函数进行初始化。
              - 可以在多处调用 init()，其内有静态变量，确保只会初始化一次。
        */
        static void init();

    private:
        UnderlyingType m_value;                 // 底层键码值
        static StateMap s_keyStates;            // 键码状态映射表

    public:
        // 提供相等比较
        constexpr bool operator==(const KeyCode other) const { return m_value == other.m_value; }
        constexpr bool operator==(const UnderlyingType other) const { return m_value == other; }
        constexpr bool operator!=(const KeyCode other) const { return m_value != other.m_value; }
        constexpr bool operator!=(const UnderlyingType other) const { return m_value != other; }

    public:
        bool is_ctrl()  const;
        bool is_shift() const;
        bool is_alt()   const;
        bool is_super() const;

#ifdef ID_USE_IMGUI
        // ImGuiKey to_ImGui_key() const;
#endif
    };

    namespace KeyCodes
    {
        inline constexpr KeyCode Unknown{-1};

        inline constexpr KeyCode Space{32};
        inline constexpr KeyCode Apostrophe{39};
        inline constexpr KeyCode Comma{44};
        inline constexpr KeyCode Minus{45};
        inline constexpr KeyCode Period{46};
        inline constexpr KeyCode Slash{47};

        inline constexpr KeyCode D0{48};
        inline constexpr KeyCode D1{49};
        inline constexpr KeyCode D2{50};
        inline constexpr KeyCode D3{51};
        inline constexpr KeyCode D4{52};
        inline constexpr KeyCode D5{53};
        inline constexpr KeyCode D6{54};
        inline constexpr KeyCode D7{55};
        inline constexpr KeyCode D8{56};
        inline constexpr KeyCode D9{57};

        inline constexpr KeyCode Semicolon{59};
        inline constexpr KeyCode Equal{61};

        inline constexpr KeyCode A{65};
        inline constexpr KeyCode B{66};
        inline constexpr KeyCode C{67};
        inline constexpr KeyCode D{68};
        inline constexpr KeyCode E{69};
        inline constexpr KeyCode F{70};
        inline constexpr KeyCode G{71};
        inline constexpr KeyCode H{72};
        inline constexpr KeyCode I{73};
        inline constexpr KeyCode J{74};
        inline constexpr KeyCode K{75};
        inline constexpr KeyCode L{76};
        inline constexpr KeyCode M{77};
        inline constexpr KeyCode N{78};
        inline constexpr KeyCode O{79};
        inline constexpr KeyCode P{80};
        inline constexpr KeyCode Q{81};
        inline constexpr KeyCode R{82};
        inline constexpr KeyCode S{83};
        inline constexpr KeyCode T{84};
        inline constexpr KeyCode U{85};
        inline constexpr KeyCode V{86};
        inline constexpr KeyCode W{87};
        inline constexpr KeyCode X{88};
        inline constexpr KeyCode Y{89};
        inline constexpr KeyCode Z{90};

        inline constexpr KeyCode LeftBracket{91};
        inline constexpr KeyCode Backslash{92};
        inline constexpr KeyCode RightBracket{93};
        inline constexpr KeyCode GraveAccent{96};
        inline constexpr KeyCode World1{161};
        inline constexpr KeyCode World2{162};

        inline constexpr KeyCode Escape{256};
        inline constexpr KeyCode Enter{257};
        inline constexpr KeyCode Tab{258};
        inline constexpr KeyCode Backspace{259};
        inline constexpr KeyCode Insert{260};
        inline constexpr KeyCode Delete{261};
        inline constexpr KeyCode Right{262};
        inline constexpr KeyCode Left{263};
        inline constexpr KeyCode Down{264};
        inline constexpr KeyCode Up{265};
        inline constexpr KeyCode PageUp{266};
        inline constexpr KeyCode PageDown{267};
        inline constexpr KeyCode Home{268};
        inline constexpr KeyCode End{269};
        inline constexpr KeyCode CapsLock{280};
        inline constexpr KeyCode ScrollLock{281};
        inline constexpr KeyCode NumLock{282};
        inline constexpr KeyCode PrintScreen{283};
        inline constexpr KeyCode Pause{284};

        inline constexpr KeyCode F1{290};
        inline constexpr KeyCode F2{291};
        inline constexpr KeyCode F3{292};
        inline constexpr KeyCode F4{293};
        inline constexpr KeyCode F5{294};
        inline constexpr KeyCode F6{295};
        inline constexpr KeyCode F7{296};
        inline constexpr KeyCode F8{297};
        inline constexpr KeyCode F9{298};
        inline constexpr KeyCode F10{299};
        inline constexpr KeyCode F11{300};
        inline constexpr KeyCode F12{301};

        inline constexpr KeyCode KP0{320};
        inline constexpr KeyCode KP1{321};
        inline constexpr KeyCode KP2{322};
        inline constexpr KeyCode KP3{323};
        inline constexpr KeyCode KP4{324};
        inline constexpr KeyCode KP5{325};
        inline constexpr KeyCode KP6{326};
        inline constexpr KeyCode KP7{327};
        inline constexpr KeyCode KP8{328};
        inline constexpr KeyCode KP9{329};
        inline constexpr KeyCode KPDecimal{330};
        inline constexpr KeyCode KPDivide{331};
        inline constexpr KeyCode KPMultiply{332};
        inline constexpr KeyCode KPSubtract{333};
        inline constexpr KeyCode KPAdd{334};
        inline constexpr KeyCode KPEnter{335};
        inline constexpr KeyCode KPEqual{336};

        inline constexpr KeyCode LeftShift{340};
        inline constexpr KeyCode LeftControl{341};
        inline constexpr KeyCode LeftAlt{342};
        inline constexpr KeyCode LeftSuper{343};
        inline constexpr KeyCode RightShift{344};
        inline constexpr KeyCode RightControl{345};
        inline constexpr KeyCode RightAlt{346};
        inline constexpr KeyCode RightSuper{347};
        inline constexpr KeyCode Menu{348};

        inline constexpr KeyCode Last = Menu;
    } // namespace KeyCodes

    // KeyCode 内联方法的实现（放在 KeyCodes 之后，因为依赖其常量）
    inline bool KeyCode::is_ctrl()  const { return m_value == KeyCodes::LeftControl  || m_value == KeyCodes::RightControl; }
    inline bool KeyCode::is_shift() const { return m_value == KeyCodes::LeftShift    || m_value == KeyCodes::RightShift; }
    inline bool KeyCode::is_alt()   const { return m_value == KeyCodes::LeftAlt      || m_value == KeyCodes::RightAlt; }
    inline bool KeyCode::is_super() const { return m_value == KeyCodes::LeftSuper    || m_value == KeyCodes::RightSuper; }
} // namespace ID
