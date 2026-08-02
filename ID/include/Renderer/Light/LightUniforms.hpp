#pragma once

#include "IDpch.hpp"

namespace ID
{
    struct alignas(16) LightDataGLSL
    {
        // vec4 color intensity
        float color_r = 1.0f;
        float color_g = 1.0f;
        float color_b = 1.0f;
        float intensity = 1.0f;

        // vec4 drop(direction or position) type
        float drop_x = 0.0f;
        float drop_y = -1.0f;
        float drop_z = 0.0f;
        float type_raw = 0.0f;

        // vec4 inner_cone_angle outer_cone_angle
        float inner_cone_angle = 15.0f;
        float outer_cone_angle = 30.0f;
        float range = 100.0f;
        float falloff = 1.0f;                   // 衰减

        float enabled = 1.0f;                        // 光源是否启用
        float _pad0;
        float _pad1;
        float _pad2;
    };

    static_assert(sizeof(LightDataGLSL) == 64, "std140 要求 LightDataGLSL 大小为 64 字节");

    
} // namespace ID