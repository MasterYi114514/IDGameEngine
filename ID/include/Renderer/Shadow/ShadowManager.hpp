#pragma once

#include "IDpch.hpp"
#include "Renderer/Shadow/ShadowConfig.hpp"
#include "Renderer/Shadow/ShadowCamera.hpp"
#include "Renderer/Shadow/ShadowMap.hpp"

namespace ID
{
    class ShadowManager;

    using ShadowMapID = BasicID<uint32_t, ShadowManager>;

    class ID_API ShadowManager
    {
    public:
        ShadowManager() = delete;
        ~ShadowManager() = delete;
        
    public:
        static ShadowMapID create(FrameBufferID fb = FrameBufferID::invalid_id(), 
            ShadowMapType map_type = ShadowMapType::Texture2D);

        static ShadowMap*  get_shadow_map(ShadowMapID id);
        static void        destroy_shadow_map(ShadowMapID id);
    };
} // namespace ID