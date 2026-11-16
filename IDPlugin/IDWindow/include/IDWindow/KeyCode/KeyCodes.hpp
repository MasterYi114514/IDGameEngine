#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "Core/Core.hpp"

#include "IDWindow/KeyCode/KeyCode.hpp"

/**
 *  键盘键与鼠标键的具名常量、域常量与成员判定。
 *
 *  常量取值属于本插件自己的稳定值域（口径见 `KeyCode.hpp`）；消费者按名字用常量，不写裸数值。
 *
 *  维护清单：新增一个具名常量时必须同步维护
 *      1. 本文件的常量块（按数值升序放置，顺序由下方排序断言锁定）
 *      2. 键盘域常量 `kKeyboardFirst` / `kKeyboardLast` 或鼠标域常量 `kMouseFirst` / `kMouseLast`
 *      3. 键盘常量还要按升序插入 `kKeyboardKeys` 判定表（成员判定只认表内条目）
 *
 *  不变量：
 *  1. 成员判定是二分查找，表项**直接引用本文件的具名常量**，不复制数值、不另立常量副本。
 *  2. 键盘常量所在的 `[kKeyboardFirst, kKeyboardLast]` 内含未声明的跳号：范围检查不够，
 *     入站校验必须走 `is_keyboard_key` 的成员判定。
 *  3. 判定函数都是 `ID_FORCEINLINE constexpr noexcept`，既可在运行期按事件载荷调用，也可在编译期用。
 */

namespace ID::Window::KeyCodes
{
    inline constexpr KeyCode Unknown{KeyCode::invalid_keycode()};

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

    inline constexpr KeyCode MouseButton1{0};
    inline constexpr KeyCode MouseButton2{1};
    inline constexpr KeyCode MouseButton3{2};
    inline constexpr KeyCode MouseButton4{3};
    inline constexpr KeyCode MouseButton5{4};
    inline constexpr KeyCode MouseButton6{5};
    inline constexpr KeyCode MouseButton7{6};
    inline constexpr KeyCode MouseButton8{7};

    inline constexpr KeyCode MouseButtonLeft{MouseButton1};
    inline constexpr KeyCode MouseButtonRight{MouseButton2};
    inline constexpr KeyCode MouseButtonMiddle{MouseButton3};
    inline constexpr KeyCode MouseButtonX1{MouseButton4};
    inline constexpr KeyCode MouseButtonX2{MouseButton5};

    inline constexpr IDKeyCodeUnderlyingType kKeyboardFirst = 32;      // 键盘域下界（含）
    inline constexpr IDKeyCodeUnderlyingType kKeyboardLast  = 348;     // 键盘域上界（含），域内含未声明的跳号
    inline constexpr IDKeyCodeUnderlyingType kMouseFirst    = 0;       // 鼠标键域下界（含）
    inline constexpr IDKeyCodeUnderlyingType kMouseLast     = 7;       // 鼠标键域上界（含）

    /**
     *  已声明键盘键的判定表：按数值升序，表项直接引用上方具名常量。
     *  新增键盘常量时必须按升序插入本表，否则成员判定不认识该键（并触发下方排序断言）。
     */
    inline constexpr std::array kKeyboardKeys{
        Space, Apostrophe, Comma, Minus, Period, Slash,
        D0, D1, D2, D3, D4, D5, D6, D7, D8, D9,
        Semicolon, Equal,
        A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
        LeftBracket, Backslash, RightBracket, GraveAccent, World1, World2,
        Escape, Enter, Tab, Backspace, Insert, Delete,
        Right, Left, Down, Up, PageUp, PageDown, Home, End,
        CapsLock, ScrollLock, NumLock, PrintScreen, Pause,
        F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
        KP0, KP1, KP2, KP3, KP4, KP5, KP6, KP7, KP8, KP9,
        KPDecimal, KPDivide, KPMultiply, KPSubtract, KPAdd, KPEnter, KPEqual,
        LeftShift, LeftControl, LeftAlt, LeftSuper,
        RightShift, RightControl, RightAlt, RightSuper, Menu,
    };

    namespace Detail
    {
        // 判定表必须严格升序：二分查找的正确性前提
        constexpr bool is_keyboard_table_sorted() noexcept
        {
            for (std::size_t index = 1; index < kKeyboardKeys.size(); ++index)
            {
                if (!(kKeyboardKeys[index - 1].get_key() < kKeyboardKeys[index].get_key()))
                {
                    return false;
                }
            }
            return true;
        }

        // 判定表的每一项都必须落在键盘域内：域常量与常量块不一致时编译失败
        constexpr bool is_keyboard_table_in_domain() noexcept
        {
            for (const KeyCode key : kKeyboardKeys)
            {
                if (key.get_key() < kKeyboardFirst || key.get_key() > kKeyboardLast)
                {
                    return false;
                }
            }
            return true;
        }
    } // namespace Detail

    static_assert(Detail::is_keyboard_table_sorted(), "kKeyboardKeys 必须按数值严格升序，否则二分查找的成员判定失效");
    static_assert(Detail::is_keyboard_table_in_domain(), "kKeyboardKeys 的每一项都必须落在 kKeyboardFirst..kKeyboardLast 内");

    /**
     *  @brief 该键码是否是**已声明**的键盘键
     *  @param key 待判定的键码
     *  @return 位于键盘域内且是判定表条目时返回 true；域内跳号与未知键值返回 false
     *  @note 入站校验必须用本函数而不是范围比较：键盘域内含未声明的跳号
     */
    ID_FORCEINLINE constexpr bool is_keyboard_key(KeyCode key) noexcept
    {
        std::size_t low  = 0;
        std::size_t high = kKeyboardKeys.size();

        while (low < high)
        {
            const std::size_t mid = (low + high) / 2;
            if (kKeyboardKeys[mid].get_key() < key.get_key())
            {
                low = mid + 1;
            }
            else
            {
                high = mid;
            }
        }

        return low < kKeyboardKeys.size() && kKeyboardKeys[low] == key;
    }

    /**
     *  @brief 该键码是否是鼠标键
     *  @param key 待判定的键码
     *  @return 落在 `[kMouseFirst, kMouseLast]` 内时返回 true
     *  @note 鼠标键域与键盘域不重叠，两个判定互斥
     */
    ID_FORCEINLINE constexpr bool is_mouse_button(KeyCode key) noexcept
    {
        return key.get_key() >= kMouseFirst && key.get_key() <= kMouseLast;
    }
} // namespace ID::Window::KeyCodes
