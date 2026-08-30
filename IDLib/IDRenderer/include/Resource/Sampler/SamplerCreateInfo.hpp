#pragma once

#include "Core/IDRpch.hpp"
#include "Resource/Sampler/SamplerParam.hpp"

namespace ID
{
    /*
    *   SamplerCreateInfo — Sampler Object 创建参数（纯数据）。
    *   sampler 对象状态优先于纹理对象参数，用于同一纹理的双采样状态（raw / compare）。
    */
    struct IDR_API SamplerCreateInfo
    {
        SamplerCreateInfo() = delete;
        SamplerCreateInfo(TextureFilter filter, TextureCompare cmp = TextureCompare::None,
            TextureWrap wrap = TextureWrap::ClampToBorder)
            : filter_min(filter), filter_mag(filter), wrap_s(wrap), wrap_t(wrap), compare(cmp) { }

        TextureFilter  filter_min      = TextureFilter::Nearest;
        TextureFilter  filter_mag      = TextureFilter::Nearest;
        TextureWrap    wrap_s          = TextureWrap::ClampToBorder;
        TextureWrap    wrap_t          = TextureWrap::ClampToBorder;
        TextureCompare compare         = TextureCompare::None;
        float          border_color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };   // 越界采样 = 1.0（阴影贴图约定）
    };
} // namespace ID
