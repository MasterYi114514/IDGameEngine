#pragma once

#include <cstdint>
#include <concepts>
#include <type_traits>

namespace ID::Common
{
    /**
     *  @brief 轻量句柄，建议对应一个数组的下标
     *  @note Handle 的实际数据只能来自于 Factory，可在外部创建的 Handle 对象只能是无效的 Handle 或对已有数据的复制、移动
     *  @note Handle 底层只有 m_id 一个成员变量，建议使用 const Handle 进行传递，无需传引用、指针
     */
    template <std::unsigned_integral UintType, typename Factory>
    class Handle
    {
        static_assert(std::is_class_v<Factory>, "Factory must be a class type");
        
    public:
        using UnderlyingType = UintType;

        constexpr Handle() noexcept : m_id(static_cast<UintType>(-1)) {}
        constexpr ~Handle() noexcept = default;
        constexpr Handle(const Handle&) noexcept = default;
        constexpr Handle& operator=(const Handle&) noexcept = default;
        constexpr Handle(Handle&&) noexcept = default;
        constexpr Handle& operator=(Handle&&) noexcept = default;

        static constexpr Handle invalid_handle() noexcept { return Handle(static_cast<UintType>(-1)); }
        constexpr bool is_valid() const noexcept { return m_id != static_cast<UintType>(-1); }
        constexpr UintType get_id() const noexcept { return m_id; }

        bool operator==(const Handle& other) const noexcept { return m_id == other.m_id; }
        bool operator!=(const Handle& other) const noexcept { return m_id != other.m_id; }
        bool operator==(const UintType& id) const noexcept { return m_id == id; }
        bool operator!=(const UintType& id) const noexcept { return m_id != id; }

    private:
        friend Factory;
        constexpr explicit Handle(UintType id) noexcept : m_id(id) {}

        UintType m_id{ static_cast<UintType>(-1) };         // 默认无效 ID，使用最大值表示无效
    };
} // namespace ID::Common