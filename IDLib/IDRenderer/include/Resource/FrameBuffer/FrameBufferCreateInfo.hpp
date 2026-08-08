#pragma once

#include "Resource/Texture/TextureCreateInfo.hpp"

namespace ID
{
    struct IDR_API FrameBufferCreateInfo
    {
        FrameBufferCreateInfo() = delete;
        FrameBufferCreateInfo(uint32_t w, uint32_t h) : width(w), height(h) { }

        uint32_t        width;                              // 帧缓冲宽度
        uint32_t        height;                             // 帧缓冲高度
        bool            has_depth_attachment = true;        // 是否有深度附件
        uint32_t        samples = 1;                        // 多重采样数量
        TextureFormat   color_format = TextureFormat::RGBA8; // 颜色附件格式
    };
} // namespace ID