#pragma once

#include <cstdint>
#include <type_traits>

/**
 *  事件分类：位标志枚举（名字）+ 位集合包装类（组合值的载体）。
 *
 *  形态依据：`skills/cpp-style/references/design-patterns.md` 第 2 节；`WindowStatus` 沿用同一形态。
 *
 *  不变量：
 *  1. 枚举里只有单值与 `None`，没有组合值；组合由 `operator|` 返回包装类型表达。
 *  2. 包装类只有位集合字一个成员；判定只用 `is_none()` 与 `is_in_category()`。
 *  3. 从裸位构造是 `explicit`：整数不会被静默当成分类集合。
 */

namespace ID::Window::Event
{
    using IDEventCategoryUINT = std::uint8_t;

    enum class EventCategoryEnum : IDEventCategoryUINT
    {
        None     = 0,           // 不属于任何分类
        Input    = 1u << 0,     // 输入事件（键盘、鼠标）
        Keyboard = 1u << 1,     // 键盘事件
        Mouse    = 1u << 2,     // 鼠标事件
        Window   = 1u << 3,     // 窗口事件
    };

    /**
     *  @brief 事件分类的位集合
     *  @note 只描述事件属于哪些分类，不承载成功/失败语义
     */
    class EventCategory
    {
    public:
        constexpr EventCategory() noexcept
            : m_category_bits(kNone)
        {
        }

        /**
         *  @brief 用裸位集合构造
         *  @param bits 位集合字；合法取值是 `EventCategoryEnum` 各单值的按位或，本构造函数不做校验
         *  @note 显式构造：禁止整数被静默当成分类集合
         */
        explicit constexpr EventCategory(IDEventCategoryUINT bits) noexcept
            : m_category_bits(bits)
        {
        }

        constexpr EventCategory(EventCategoryEnum category) noexcept
            : m_category_bits(static_cast<IDEventCategoryUINT>(category))
        {
        }

        constexpr EventCategory(const EventCategory& other) noexcept = default;
        constexpr EventCategory& operator=(const EventCategory& other) noexcept = default;
        constexpr EventCategory(EventCategory&& other) noexcept = default;
        constexpr EventCategory& operator=(EventCategory&& other) noexcept = default;

        constexpr bool is_none() const noexcept
        {
            return m_category_bits == kNone;
        }

        constexpr bool is_in_category(EventCategoryEnum category) const noexcept
        {
            return (m_category_bits & static_cast<IDEventCategoryUINT>(category)) != 0;
        }

    private:
        static inline constexpr IDEventCategoryUINT kNone = static_cast<IDEventCategoryUINT>(EventCategoryEnum::None);

        IDEventCategoryUINT m_category_bits;        // 位集合字；空分类为 0
    };

    static_assert(sizeof(EventCategoryEnum) == sizeof(IDEventCategoryUINT), "EventCategoryEnum 的底层类型必须与位集合字同宽");
    static_assert(sizeof(EventCategory) == sizeof(IDEventCategoryUINT), "EventCategory 必须是单个位集合字，不能带其它状态");
    static_assert(std::is_trivially_copyable_v<EventCategory>, "EventCategory 必须可平凡复制，否则不能跨模块按值传递");

    constexpr EventCategory operator|(EventCategoryEnum lhs, EventCategoryEnum rhs) noexcept
    {
        const IDEventCategoryUINT bits = static_cast<IDEventCategoryUINT>(lhs) | static_cast<IDEventCategoryUINT>(rhs);
        return EventCategory{bits};
    }
} // namespace ID::Window::Event
