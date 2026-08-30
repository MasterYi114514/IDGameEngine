#pragma once

#include "IDpch.hpp"
#include "Renderer/Shadow/ShadowConfig.hpp"   // MAX_CASCADES（唯一事实来源）

namespace ID
{
    // 光照模型（对应 shader uniform u_lighting_model 取值）
    enum class LightingModel : uint8_t
    {
        Phong = 0,
        BlinnPhong = 1,
        PBR = 2
    };

    // 阴影滤波（对应 shader uniform u_shadow_filter 取值）
    enum class ShadowFilter : uint8_t
    {
        Hard = 0,
        PCF = 1,
        PCSS = 2
    };

    struct ID_API RendererSettings
    {
        // 光照模型
        LightingModel lighting_model = LightingModel::BlinnPhong;
        float         metallic       = 0.5f;
        float         roughness      = 0.5f;

        // 阴影滤波
        ShadowFilter shadow_filter = ShadowFilter::PCF;
        float        light_size    = 10.0f;

        // CSM（唯一事实来源；ShadowPass 每帧读取）
        uint32_t cascade_count       = 1;         // 1 = 关闭 CSM
        float    cascade_lambda      = 0.75f;     // PSSM 分割系数（0 均匀 / 1 对数）
        float    cascade_far_override = 0.0f;     // 0 = 跟随相机 far_z；>0 手动收紧
    };

    /**
     *  获取 RendererSettings 单例实例
     *  并没有对 RendererSettings 类进行限定，仍然可以创建其它实例
     */
    RendererSettings& ID_API get_renderer_settings();
} // namespace ID
