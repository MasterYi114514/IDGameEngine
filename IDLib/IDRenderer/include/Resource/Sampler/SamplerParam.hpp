#pragma once

#include "Core/IDRpch.hpp"
#include "Resource/Texture/TextureCreateInfo.hpp"

namespace ID
{
    // 深度比较模式（Sampler Object 专用；GL_TEXTURE_COMPARE_MODE 由 sampler 对象控制）
    enum class TextureCompare : uint8_t
    {
        None = 0,           // 普通采样（raw：读原始深度值）
        RefToTexture = 1    // 与纹理深度比较（GL_COMPARE_REF_TO_TEXTURE，硬件 PCF）
    };
} // namespace ID
