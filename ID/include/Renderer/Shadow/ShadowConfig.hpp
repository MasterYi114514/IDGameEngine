#pragma once


#include "Renderer/Light/Light.hpp"


namespace ID
{
    enum class ShadowQuality : uint8_t
    {
        Low     = 0,    //  512², 无 PCF
        Medium  = 1,    // 1024², 3×3 PCF
        High    = 2,    // 2048², 5×5 PCF
        Ultra   = 3     // 4096², 7×7 PCF
    };

    inline constexpr uint32_t shadow_quality_to_map_size(ShadowQuality quality)
    {
        constexpr uint32_t sizes[] = { 512, 1024, 2048, 4096 };
        return sizes[static_cast<uint8_t>(quality)];
    }

    inline constexpr uint32_t shadow_quality_to_pcf_kernel_size(ShadowQuality quality)
    {
        // 0 -> 1*1, 1 -> 3*3, 2 -> 5*5, 3 -> 7*7
        constexpr uint32_t kernel_sizes[] = { 0, 1, 2, 3 };
        return kernel_sizes[static_cast<uint8_t>(quality)];
    }

    struct ShadowParam
    {
        ShadowQuality quality = ShadowQuality::High;    // 阴影质量
        float         bias = 0.0002f;                   // 阴影偏移（防止自阴影）
        float         normal_bias = 0.02f;             // 法线偏移（防止自阴影）
    };

    template<LightType LType>
    struct ShadowConfig;

    template<>
    struct ShadowConfig<LightType::Directional>
    {
        ShadowParam param;
        float       ortho_extent = 20.0f;               // 正交投影半范围：[-extent, extent]，控制阴影覆盖区域
        // uint32_t    cascade_count = 1;                  // 级联阴影贴图数量
        float       cascade_splits_lambda = 0.75f;      // 级联分割比例，0.0 为均匀分割，1.0 为对数分割
        float       near_plane = 0.5f;                  // 光源视锥近平面
        float       far_plane = 300.0f;                 // 光源视锥远平面（需覆盖场景纵深）
    };

    template<>
    struct ShadowConfig<LightType::Point>
    {
        ShadowParam param;
        float       near_plane = 0.1f;                  // 光源视锥近平面
        float       far_plane = 300.0f;                 // 光源视锥远平面（需覆盖场景纵深）
    };

    template<>
    struct ShadowConfig<LightType::Spot>
    {
        ShadowParam param;
        float       near_plane = 0.1f;                  // 光源视锥近平面
        float       far_plane = 300.0f;                 // 光源视锥远平面（需覆盖场景纵深）
    };

    using DirectionalShadowConfig = ShadowConfig<LightType::Directional>;
    using PointShadowConfig       = ShadowConfig<LightType::Point>;

} // namespace ID