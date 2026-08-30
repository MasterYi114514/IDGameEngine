#pragma once

#include "Core/IDRpch.hpp"
// #include "Resource/Texture/TextureLoader.hpp"

namespace ID
{
    enum class TextureFormat : uint8_t
    {
        None    = 0,
        R8      = 1,
        RG8     = 2,
        RGB8    = 3,
        RGBA8   = 4,
        RGBA16F = 5,
        Depth   = 6
    };

    enum class TextureFilter : uint8_t
    {
        Nearest, Linear, Trilinear
    };

    enum class TextureWrap : uint8_t
    {
        Repeat, ClampToEdge, ClampToBorder
    };

    struct IDR_API TextureCreateInfo
    {
        TextureCreateInfo() = delete;
        TextureCreateInfo(uint32_t w, uint32_t h, const void* data = nullptr, TextureFormat fmt  = TextureFormat::RGBA8) : width(w), height(h), format(fmt), pixel_data(data) { }

        // TextureCreateInfo(const TextureData& texture_data, TextureFormat fmt = TextureFormat::RGBA8)
        //     : width(texture_data.width), height(texture_data.height),
        //       format(fmt), pixel_data(texture_data.data) { }

        uint32_t      width      = 0;
        uint32_t      height     = 0;
        uint32_t      layers     = 1;          // 纹理层数；>1 → GL_TEXTURE_2D_ARRAY（CSM 级联阴影等分层纹理）
        bool          is_array   = false;      // 强制 array 形态（1 层亦为 GL_TEXTURE_2D_ARRAY；ShadowMap 统一形态用）
        TextureFormat format     = TextureFormat::RGBA8;
        TextureFilter filter     = TextureFilter::Linear;
        TextureWrap   wrap_s     = TextureWrap::Repeat;
        TextureWrap   wrap_t     = TextureWrap::Repeat;
        const void*   pixel_data = nullptr;
        bool          gen_mips   = true;
    };
}