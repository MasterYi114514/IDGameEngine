#pragma once

#include "Core/IDRpch.hpp"
#include "Resource/Texture/TextureCreateInfo.hpp"

namespace ID
{
    /*
    *   FrameBufferCreateInfo 描述帧缓冲的创建参数。
    *   color_formats 为颜色附件格式数组（空 = 纯深度 FBO），
    *   附件数上限 8（构造时校验截断），TextureFormat::Depth 不允许出现在颜色附件中（构造时剔除）。
    */
    struct IDR_API FrameBufferCreateInfo
    {
        FrameBufferCreateInfo() = delete;

        // 单附件便捷构造（默认 RGBA8，现有调用点最小改动）
        FrameBufferCreateInfo(uint32_t w, uint32_t h, TextureFormat fmt = TextureFormat::RGBA8);

        // MRT 构造：多颜色附件（上限 8，构造时校验）
        FrameBufferCreateInfo(uint32_t w, uint32_t h, std::vector<TextureFormat> fmts);

        uint32_t                     width                = 0;
        uint32_t                     height               = 0;
        std::vector<TextureFormat>   color_formats;       // 颜色附件格式数组（空 = 纯深度 FBO）
        bool                         has_depth_attachment = true;
        uint32_t                     samples              = 1;
    };
} // namespace ID
