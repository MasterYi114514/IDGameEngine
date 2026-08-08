#pragma once

#include "IDpch.hpp"
#include "Renderer/IDRCore.hpp"
#include "Renderer/Shadow/ShadowConfig.hpp"
#include "Renderer/Shadow/ShadowCamera.hpp"

namespace ID
{
    enum class ShadowMapType : uint8_t
    {
        Texture2D = 0,    // 2D 阴影贴图（平行光 / 聚光灯）
        CubeMap   = 1,    // Cube 阴影贴图（点光源）
    };

    class ID_API ShadowMap
    {
    public:
        ShadowMap() = default;
        explicit ShadowMap(FrameBufferID fb, ShadowMapType map_type = ShadowMapType::Texture2D) 
            : m_fb(fb), m_type(map_type) {}

        ~ShadowMap() { destroy(); }

        ShadowMap(const ShadowMap&) = delete;
        ShadowMap& operator=(const ShadowMap&) = delete;
        ShadowMap(ShadowMap&& other) noexcept;
        ShadowMap& operator=(ShadowMap&& other) noexcept;

        void destroy() { m_fb = FrameBufferID::invalid_id(); }

        bool           is_valid()     const { return m_fb.is_valid(); }

        FrameBufferID  get_fb()      const { return m_fb; }
        void           set_fb(FrameBufferID fb) { m_fb = fb; }
        
        ShadowMapType  get_type()     const { return m_type; }
        void           set_type(ShadowMapType type) { m_type = type; }

    private:
        FrameBufferID m_fb = FrameBufferID::invalid_id();    // 阴影贴图的 FrameBuffer
        ShadowMapType m_type = ShadowMapType::Texture2D;     // 阴影贴图类型
    };
} // namespace ID