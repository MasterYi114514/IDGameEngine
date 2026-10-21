#pragma once

#include <cstdint>
#include <concepts>

namespace ID
{
    template <std::unsigned_integral UintType, typename Factory>
    class IDHandle
    {
        static_assert(std::is_class_v<Factory>, "Factory must be a class type");
        
    public:
        using UnderlyingType = UintType;

        constexpr IDHandle() noexcept : m_id(static_cast<UintType>(-1)) {}
        constexpr ~IDHandle() noexcept = default;
        constexpr IDHandle(const IDHandle&) noexcept = default;
        constexpr IDHandle& operator=(const IDHandle&) noexcept = default;
        constexpr IDHandle(IDHandle&&) noexcept = default;
        constexpr IDHandle& operator=(IDHandle&&) noexcept = default;

        static constexpr IDHandle invalid_handle() noexcept { return IDHandle(static_cast<UintType>(-1)); }
        constexpr bool is_valid() const noexcept { return m_id != static_cast<UintType>(-1); }
        constexpr UintType get_id() const noexcept { return m_id; }

        bool operator==(const IDHandle& other) const noexcept { return m_id == other.m_id; }
        bool operator!=(const IDHandle& other) const noexcept { return m_id != other.m_id; }
        bool operator==(const UintType& id) const noexcept { return m_id == id; }
        bool operator!=(const UintType& id) const noexcept { return m_id != id; }

    private:
        friend Factory;
        constexpr explicit IDHandle(UintType id) noexcept : m_id(id) {}

        UintType m_id{ static_cast<UintType>(-1) };         // 默认无效 ID，使用最大值表示无效
    };
} // namespace ID