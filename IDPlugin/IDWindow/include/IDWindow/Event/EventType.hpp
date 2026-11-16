#pragma once

#include <cstdint>
#include <type_traits>

#include "Core/Core.hpp"

/**
 *  事件类型标识：数值是稳定契约，`None = 0` 表示"没有事件"，其余按窗口、键盘、鼠标、文本分组编号。
 *
 *  维护清单：新增、修改或删除一个事件类型时必须同步维护
 *      1. 本文件的枚举值与末尾的 `Count`（`Count` 是哨兵，不作合法下标）
 *      2. `Event/Event.hpp` 的 traits 特化表、payload concept 列表与接线断言
 *      3. `Event/EventContext.hpp` 的 union 成员、字段表与 `write_field` 重载
 *      4. `Window::on_event` 的分派 `switch` 与 `Window` 的回调表槽位
 *
 *  不变量：
 *  1. 既有取值不重排：新事件只允许**尾部追加**；`Count` 永远在最后且等于合法项个数。
 *  2. 底层宽度是 `std::uint8_t`，追加时先确认余量。
 */
namespace ID::Window::Event
{
    using IDEventTypeUINT = std::uint8_t;

    /**
     *  @brief 事件类型标识
     *  @note 数值是稳定契约；`None = 0` 表示"没有事件"，其余按窗口、键盘、鼠标、文本分组编号
     */
    enum class EventType : IDEventTypeUINT
    {
        None                        = 0,    // 没有事件 / 无效事件

        // 窗口事件
        WindowClose                 = 1,
        WindowResize                = 2,    // 逻辑尺寸
        WindowFocus                 = 3,
        WindowLostFocus             = 4,
        WindowMoved                 = 5,

        // 键盘事件
        KeyPressed                  = 6,
        KeyReleased                 = 7,
        KeyRepeat                   = 8,

        // 鼠标事件
        MouseButtonPressed          = 9,
        MouseButtonReleased         = 10,
        MouseMoved                  = 11,
        MouseScrolled               = 12,

        // 窗口事件（帧缓冲、缩放、窗口系统状态）
        WindowFramebufferResize     = 13,   // 像素尺寸，与逻辑尺寸的 WindowResize 区分
        WindowContentScaleChanged   = 14,
        WindowMinimized             = 15,
        WindowRestored              = 16,
        WindowMaximized             = 17,
        WindowUnmaximized           = 18,
        WindowRefresh               = 19,
        WindowCursorEntered         = 20,
        WindowCursorLeft            = 21,

        // 文本与拖放
        TextInput                   = 22,
        FileDrop                    = 23,

        // 哨兵：合法项个数，不是合法下标
        Count                       = 24,
    };

    static_assert(sizeof(EventType) == sizeof(IDEventTypeUINT), "EventType 的底层类型必须与转换函数的返回类型一致");
    static_assert(static_cast<IDEventTypeUINT>(EventType::Count) == 24, "EventType::Count 必须等于合法项个数，追加事件类型时同步本断言");

    ID_FORCEINLINE constexpr IDEventTypeUINT to_event_type_uint(EventType type) noexcept
    {
        return static_cast<IDEventTypeUINT>(type);
    }

    ID_FORCEINLINE constexpr EventType to_event_type(IDEventTypeUINT type) noexcept
    {
        return static_cast<EventType>(type);
    }
} // namespace ID::Window::Event
