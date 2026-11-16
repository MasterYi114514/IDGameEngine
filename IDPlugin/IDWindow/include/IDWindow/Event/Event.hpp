#pragma once

#include <cstddef>
#include <type_traits>
#include <utility>

#include "IDWindow/Event/EventCategory.hpp"
#include "IDWindow/Event/EventData.hpp"
#include "IDWindow/Event/EventType.hpp"
#include "IDWindow/KeyCode/KeyCode.hpp"
#include "IDWindow/Mouse.hpp"

/**
 *  事件类型 traits 表与按类型携带负载的值类型 `Event<T>`。
 *
 *  payload 集合（共 9 个）：`EmptyPayload`、`KeyCode`、`WindowSize`、`WindowPosition`、`WindowScale`、
 *  `Mouse::Position`、`Mouse::ScrollOffset`、`Codepoint`、`FileDropList`。
 *  新增、修改或删除一个 payload 类型时必须同步维护：
 *      1. 本文件的 traits 特化表（`ID_EVENT_TRAITS_IMPL` 行）
 *      2. `IsEventPayloadType` 的 concept 列表
 *      3. `Event/EventContext.hpp` 的 union 成员、`ContextField` 特化与 `write_field` 重载
 *      4. 本文件末尾的全枚举接线断言与 `EventContext.hpp` 的 roundtrip 断言
 *
 *  不变量：
 *  1. 主模板 `EventTraits` 只声明不定义：某个 `EventType` 取值缺特化时使用它即编译失败。
 *  2. 特化只承载两项编译期信息（`payload` 别名与 `kCategory`），没有运行期成员。
 *  3. 接线断言只证明"每个枚举值都有特化、别名可解析、payload 可用、分类与 `None` 语义一致"；
 *     字节级往返由 `EventContext` 的 roundtrip 断言负责，两者不互相代替。
 */

#define ID_EVENT_TRAITS_IMPL(event_type, payload_type, category)                    \
    template <>                                                                     \
    struct EventTraits<event_type>                                                  \
    {                                                                               \
        EventTraits() = delete;                                                     \
        using payload                                       = payload_type;         \
        static inline constexpr EventCategory   kCategory   = category;             \
    };

namespace ID::Window::Event
{
    /**
     *  @brief 没有负载的事件类型的负载占位
     */
    struct EmptyPayload
    {
    };

    static_assert(std::is_trivially_copyable_v<EmptyPayload>, "EmptyPayload 必须可平凡复制");
    static_assert(std::is_standard_layout_v<EmptyPayload>, "EmptyPayload 必须标准布局");

    /**
     *  @brief 单个事件类型的负载与分类
     *  @tparam T 事件类型；每个合法取值都由本文件的特化给出，未特化即编译失败
     */
    template <EventType T>
    struct EventTraits;

    template <typename T>
    concept IsEventPayloadType = std::is_same_v<T, EmptyPayload>
                              || std::is_same_v<T, KeyCode>
                              || std::is_same_v<T, WindowSize>
                              || std::is_same_v<T, WindowPosition>
                              || std::is_same_v<T, WindowScale>
                              || std::is_same_v<T, Mouse::Position>
                              || std::is_same_v<T, Mouse::ScrollOffset>
                              || std::is_same_v<T, Codepoint>
                              || std::is_same_v<T, FileDropList>;

    template <EventType T>
    requires IsEventPayloadType<typename EventTraits<T>::payload>
    using PayloadOfEventType = typename EventTraits<T>::payload;

    ID_EVENT_TRAITS_IMPL(EventType::None, EmptyPayload, EventCategoryEnum::None);

    // 窗口事件
    ID_EVENT_TRAITS_IMPL(EventType::WindowClose, EmptyPayload, EventCategoryEnum::Window);
    ID_EVENT_TRAITS_IMPL(EventType::WindowResize, WindowSize, EventCategoryEnum::Window);
    ID_EVENT_TRAITS_IMPL(EventType::WindowFocus, EmptyPayload, EventCategoryEnum::Window);
    ID_EVENT_TRAITS_IMPL(EventType::WindowLostFocus, EmptyPayload, EventCategoryEnum::Window);
    ID_EVENT_TRAITS_IMPL(EventType::WindowMoved, WindowPosition, EventCategoryEnum::Window);

    // 键盘事件
    ID_EVENT_TRAITS_IMPL(EventType::KeyPressed, KeyCode, EventCategoryEnum::Input | EventCategoryEnum::Keyboard);
    ID_EVENT_TRAITS_IMPL(EventType::KeyReleased, KeyCode, EventCategoryEnum::Input | EventCategoryEnum::Keyboard);
    ID_EVENT_TRAITS_IMPL(EventType::KeyRepeat, KeyCode, EventCategoryEnum::Input | EventCategoryEnum::Keyboard);

    // 鼠标事件
    ID_EVENT_TRAITS_IMPL(EventType::MouseButtonPressed, KeyCode, EventCategoryEnum::Input | EventCategoryEnum::Mouse);
    ID_EVENT_TRAITS_IMPL(EventType::MouseButtonReleased, KeyCode, EventCategoryEnum::Input | EventCategoryEnum::Mouse);
    ID_EVENT_TRAITS_IMPL(EventType::MouseMoved, Mouse::Position, EventCategoryEnum::Input | EventCategoryEnum::Mouse);
    ID_EVENT_TRAITS_IMPL(EventType::MouseScrolled, Mouse::ScrollOffset, EventCategoryEnum::Input | EventCategoryEnum::Mouse);

    // 窗口事件（帧缓冲、缩放、窗口系统状态）
    ID_EVENT_TRAITS_IMPL(EventType::WindowFramebufferResize, WindowSize, EventCategoryEnum::Window);
    ID_EVENT_TRAITS_IMPL(EventType::WindowContentScaleChanged, WindowScale, EventCategoryEnum::Window);
    ID_EVENT_TRAITS_IMPL(EventType::WindowMinimized, EmptyPayload, EventCategoryEnum::Window);
    ID_EVENT_TRAITS_IMPL(EventType::WindowRestored, EmptyPayload, EventCategoryEnum::Window);
    ID_EVENT_TRAITS_IMPL(EventType::WindowMaximized, EmptyPayload, EventCategoryEnum::Window);
    ID_EVENT_TRAITS_IMPL(EventType::WindowUnmaximized, EmptyPayload, EventCategoryEnum::Window);
    ID_EVENT_TRAITS_IMPL(EventType::WindowRefresh, EmptyPayload, EventCategoryEnum::Window);
    ID_EVENT_TRAITS_IMPL(EventType::WindowCursorEntered, EmptyPayload, EventCategoryEnum::Window);
    ID_EVENT_TRAITS_IMPL(EventType::WindowCursorLeft, EmptyPayload, EventCategoryEnum::Window);

    // 文本与拖放
    ID_EVENT_TRAITS_IMPL(EventType::TextInput, Codepoint, EventCategoryEnum::Input);
    ID_EVENT_TRAITS_IMPL(EventType::FileDrop, FileDropList, EventCategoryEnum::Input);

    /**
     *  @brief 按类型携带负载的事件值
     *  @tparam T 事件类型；`payload` 的类型由 traits 表给出
     *  @note 按值传递，可平凡复制；`type` 与 `category` 是编译期常量，不占运行期空间
     */
    template <EventType T>
    struct Event
    {
        static inline constexpr EventType       type     = T;
        static inline constexpr EventCategory   category = EventTraits<T>::kCategory;

        using PayloadType = PayloadOfEventType<T>;

        PayloadType payload{};

        constexpr Event() noexcept = default;

        constexpr explicit Event(const PayloadType& value) noexcept
            : payload(value)
        {
        }
    };

    namespace Detail
    {
        // 只校验 traits 接线：每个枚举值都有特化、payload 别名可解析且可用、分类与 None 语义一致。
        // 字节级往返由 EventContext 的 roundtrip 断言负责，本条不声称载体已覆盖 payload。
        template <EventType T>
        constexpr bool wiring_test() noexcept
        {
            using Payload = PayloadOfEventType<T>;

            static_assert(std::is_default_constructible_v<Payload>, "payload 必须可默认构造");
            static_assert(std::is_copy_constructible_v<Payload>, "payload 必须可复制");
            static_assert(std::is_trivially_copyable_v<Payload>, "payload 必须可平凡复制，否则不能整块搬运");
            static_assert(IsEventPayloadType<Payload>, "payload 必须在本文件的 payload 集合内");

            constexpr EventCategory category = EventTraits<T>::kCategory;
            return (T == EventType::None) == category.is_none();
        }

        template <std::size_t... I>
        constexpr bool wiring_test_all(std::index_sequence<I...>) noexcept
        {
            return (wiring_test<static_cast<EventType>(I)>() && ...);
        }
    } // namespace Detail

    static_assert(Detail::wiring_test_all(std::make_index_sequence<to_event_type_uint(EventType::Count)>()),
                  "EventTraits 接线不完整：每个 EventType 取值都必须有特化，且 kCategory 与 None 语义一致");
} // namespace ID::Window::Event

#undef ID_EVENT_TRAITS_IMPL
