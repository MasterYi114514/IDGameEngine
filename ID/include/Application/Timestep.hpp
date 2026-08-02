#pragma once

#include "IDpch.hpp"

namespace ID
{
    class Timestep
    {
    public:
        Timestep(float seconds = 0.0f)
            : m_seconds(seconds) { }

        // 以秒为单位
        float get_seconds()    const { return m_seconds; }
        // 以毫秒为单位
        float get_milliseconds() const { return m_seconds * 1000.0f; }

        operator float() const { return m_seconds; }

    private:
        float m_seconds = 0.0f;
    };
} // namespace ID
