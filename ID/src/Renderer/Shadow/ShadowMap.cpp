#include "Renderer/Shadow/ShadowMap.hpp"

namespace ID
{
    ShadowMap::ShadowMap(ShadowMap&& other) noexcept
        : m_fb(other.m_fb), m_type(other.m_type)
    {
        other.m_fb = FrameBufferID::invalid_id();
    }

    ShadowMap& ShadowMap::operator=(ShadowMap&& other) noexcept
    {
        if (this != &other)
        {
            std::swap(m_fb, other.m_fb);
            std::swap(m_type, other.m_type);
            other.destroy();
        }
        return *this;
    }
} // namespace ID