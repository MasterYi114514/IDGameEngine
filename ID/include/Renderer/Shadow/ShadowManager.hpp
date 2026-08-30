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
        // 创建 array 深度阴影贴图（layer_count = 1 亦走 array 路径，统一形态）
        static ShadowMapID create(uint32_t size, uint32_t layer_count = 1,
            ShadowMapType map_type = ShadowMapType::Texture2D);

        static ShadowMap*  get_shadow_map(ShadowMapID id);
        static void        destroy_shadow_map(ShadowMapID id);
    };
} // namespace ID