#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

#include "IDWindow/Event/Event.hpp"
#include "IDWindow/Event/EventData.hpp"

/**
 *  事件载体：`type` tag + 各 payload 的 union，字节级可整块搬运，不含任何动态分配。
 *
 *  形态：tagged union 类型擦除载体（`skills/cpp-style/references/design-patterns.md` 第 5 节）——
 *  tag 在偏移 0、union 紧随其后，一组 `static_assert` 钉住大小、对齐、tag 偏移与可平凡复制。
 *  写入只有一个入口（`Detail::write_field` 重载集），读取走成员指针字段表。
 *
 *  不变量：
 *  1. `type` 在偏移 0；`to_event<T>()` 相信 tag（调用方已确认类型），`try_to_event<T>()` 先校验 tag。
 *  2. 每个 payload 都有一个 union 成员；空负载也有独立成员，其字节无意义。
 *  3. 布局闸门是四条断言加全枚举 roundtrip 断言：payload 集合或 union 成员变动会让断言失败。
 *  4. 带尾部填充的 payload（指针 + 整数字段）不在 roundtrip 里比填充字节，见 `Detail::is_same_value`。
 */

namespace ID::Window::Event
{
    /**
     *  @brief 类型擦除的事件载体
     *  @note 读 union 前必须由调用方确认 `type`；`to_event()` 不重复校验
     */
    struct alignas(8) EventContext
    {
        EventType type;

        union
        {
            EmptyPayload            empty;
            KeyCode                 key_code;
            WindowSize              window_size;
            WindowPosition          window_position;
            WindowScale             window_scale;
            Mouse::Position         mouse_position;
            Mouse::ScrollOffset     mouse_scroll_offset;
            Codepoint               codepoint;
            FileDropList            file_drop;
        };

        constexpr EventContext() noexcept
            : type(EventType::None), empty{}
        {
        }

        template <EventType T>
        constexpr explicit EventContext(const Event<T>& event) noexcept;

        template <EventType T>
        [[nodiscard]] constexpr bool is() const noexcept;

        template <EventType T>
        [[nodiscard]] constexpr Event<T> to_event() const noexcept;

        template <EventType T>
        [[nodiscard]] constexpr std::optional<Event<T>> try_to_event() const noexcept;
    };

    static_assert(sizeof(EventContext) == 24, "EventContext 的容量变化会破坏跨模块布局契约与整块搬运前提");
    static_assert(alignof(EventContext) == 8, "EventContext 的对齐变化会让载体重新对齐");
    static_assert(offsetof(EventContext, type) == 0, "type 必须在偏移 0，否则字段指针表失效");
    static_assert(std::is_trivially_copyable_v<EventContext>, "EventContext 必须可平凡复制，否则不能整块搬运");

    namespace Detail
    {
        template <typename Payload>
        requires IsEventPayloadType<Payload>
        struct ContextField;

        template <>
        struct ContextField<EmptyPayload>
        {
            static inline constexpr auto kField = &EventContext::empty;
        };

        template <>
        struct ContextField<KeyCode>
        {
            static inline constexpr auto kField = &EventContext::key_code;
        };

        template <>
        struct ContextField<WindowSize>
        {
            static inline constexpr auto kField = &EventContext::window_size;
        };

        template <>
        struct ContextField<WindowPosition>
        {
            static inline constexpr auto kField = &EventContext::window_position;
        };

        template <>
        struct ContextField<WindowScale>
        {
            static inline constexpr auto kField = &EventContext::window_scale;
        };

        template <>
        struct ContextField<Mouse::Position>
        {
            static inline constexpr auto kField = &EventContext::mouse_position;
        };

        template <>
        struct ContextField<Mouse::ScrollOffset>
        {
            static inline constexpr auto kField = &EventContext::mouse_scroll_offset;
        };

        template <>
        struct ContextField<Codepoint>
        {
            static inline constexpr auto kField = &EventContext::codepoint;
        };

        template <>
        struct ContextField<FileDropList>
        {
            static inline constexpr auto kField = &EventContext::file_drop;
        };

        template <EventType T>
        inline constexpr auto kContextField = ContextField<PayloadOfEventType<T>>::kField;

        constexpr void write_field(EventContext& context, const EmptyPayload& value) noexcept
        {
            std::construct_at(&context.empty, value);
        }

        constexpr void write_field(EventContext& context, const KeyCode& value) noexcept
        {
            std::construct_at(&context.key_code, value);
        }

        constexpr void write_field(EventContext& context, const WindowSize& value) noexcept
        {
            std::construct_at(&context.window_size, value);
        }

        constexpr void write_field(EventContext& context, const WindowPosition& value) noexcept
        {
            std::construct_at(&context.window_position, value);
        }

        constexpr void write_field(EventContext& context, const WindowScale& value) noexcept
        {
            std::construct_at(&context.window_scale, value);
        }

        constexpr void write_field(EventContext& context, const Mouse::Position& value) noexcept
        {
            std::construct_at(&context.mouse_position, value);
        }

        constexpr void write_field(EventContext& context, const Mouse::ScrollOffset& value) noexcept
        {
            std::construct_at(&context.mouse_scroll_offset, value);
        }

        constexpr void write_field(EventContext& context, const Codepoint& value) noexcept
        {
            std::construct_at(&context.codepoint, value);
        }

        constexpr void write_field(EventContext& context, const FileDropList& value) noexcept
        {
            std::construct_at(&context.file_drop, value);
        }

        // 值相同判定：默认按字节比较（payload 都必须可平凡复制）。
        [[nodiscard]] constexpr bool is_same_value(const EmptyPayload&, const EmptyPayload&) noexcept
        {
            // 空负载没有值状态：它在 union 里占的那 1 字节是填充，比字节无意义，只校验可写可读
            return true;
        }

        template <typename Payload>
        requires (!std::is_empty_v<Payload>) && (!std::is_same_v<Payload, FileDropList>)
        [[nodiscard]] constexpr bool is_same_value(const Payload& lhs, const Payload& rhs) noexcept
        {
            return std::bit_cast<std::array<std::byte, sizeof(Payload)>>(lhs)
                == std::bit_cast<std::array<std::byte, sizeof(Payload)>>(rhs);
        }

        // 含指针且带尾部填充的 payload：逐字段比较，不比填充字节
        [[nodiscard]] constexpr bool is_same_value(const FileDropList& lhs, const FileDropList& rhs) noexcept
        {
            return lhs.paths == rhs.paths && lhs.count == rhs.count;
        }
    } // namespace Detail

    template <EventType T>
    constexpr EventContext::EventContext(const Event<T>& event) noexcept
        : type(T)
    {
        Detail::write_field(*this, event.payload);
    }

    template <EventType T>
    constexpr bool EventContext::is() const noexcept
    {
        return type == T;
    }

    template <EventType T>
    constexpr Event<T> EventContext::to_event() const noexcept
    {
        return Event<T>{ this->*Detail::kContextField<T> };
    }

    template <EventType T>
    constexpr std::optional<Event<T>> EventContext::try_to_event() const noexcept
    {
        if (type != T)
        {
            return std::nullopt;
        }
        return Event<T>{ this->*Detail::kContextField<T> };
    }

    namespace Detail
    {
        // 真实 roundtrip：写进 union，再按同一类型读回，最后按值比较
        template <EventType T>
        constexpr bool roundtrip_test() noexcept
        {
            using Payload = PayloadOfEventType<T>;

            const Event<T>      source{ Payload{} };
            const EventContext  context{ source };
            const Event<T>      result = context.to_event<T>();

            return context.is<T>() && is_same_value(result.payload, source.payload);
        }

        template <std::size_t... I>
        constexpr bool roundtrip_test_all(std::index_sequence<I...>) noexcept
        {
            return (roundtrip_test<static_cast<EventType>(I)>() && ...);
        }
    } // namespace Detail

    static_assert(Detail::roundtrip_test_all(std::make_index_sequence<to_event_type_uint(EventType::Count)>()),
                  "EventContext roundtrip 失败：payload 必须能写进 union 再按同一类型读回");
} // namespace ID::Window::Event
