#pragma once

#include <cstdint>
#include <concepts>

namespace ID
{
    template<std::unsigned_integral T, typename Factory>
    class BasicID
    {
        static_assert(std::is_class_v<Factory>, "Factory 必须是一个类");

    public:
        using UnderlyingType = T;

        constexpr BasicID() : m_id(static_cast<T>(-1)) { }
        constexpr ~BasicID() = default;
        constexpr BasicID(const BasicID&) = default;
        constexpr BasicID& operator=(const BasicID&) = default;
        constexpr BasicID(BasicID&&) noexcept = default;
        constexpr BasicID& operator=(BasicID&&) noexcept = default;

        static constexpr BasicID invalid_id() { return BasicID(static_cast<T>(-1)); }
        constexpr bool is_valid() const { return m_id != static_cast<T>(-1); }
        constexpr T get_id() const { return m_id; }

        bool operator==(const BasicID& other) const { return m_id == other.m_id; }
        bool operator!=(const BasicID& other) const { return m_id != other.m_id; }
        bool operator==(const T& id) const { return m_id == id; }
        bool operator!=(const T& id) const { return m_id != id; }

    private:
        friend Factory;
        constexpr explicit BasicID(T id) : m_id(id) { }

        T m_id;
    };
} // namespace ID