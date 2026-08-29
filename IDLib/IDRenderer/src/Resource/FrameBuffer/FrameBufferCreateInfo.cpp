#include "Resource/FrameBuffer/FrameBufferCreateInfo.hpp"
#include "Log/Log.hpp"

namespace ID
{
    FrameBufferCreateInfo::FrameBufferCreateInfo(uint32_t w, uint32_t h, TextureFormat fmt)
        : FrameBufferCreateInfo(w, h, std::vector<TextureFormat>{fmt})
    {
    }

    FrameBufferCreateInfo::FrameBufferCreateInfo(uint32_t w, uint32_t h, std::vector<TextureFormat> fmts)
        : width(w), height(h), color_formats(std::move(fmts))
    {
        // 校验：Depth 格式不允许出现在颜色附件中（剔除）
        for (auto it = color_formats.begin(); it != color_formats.end();)
        {
            if (*it == TextureFormat::Depth)
            {
                IDR_ERROR("FrameBufferCreateInfo: TextureFormat::Depth 不允许出现在颜色附件中，已剔除");
                it = color_formats.erase(it);
            }
            else
            {
                ++it;
            }
        }

        // 校验：颜色附件数上限 8（超限截断）
        if (color_formats.size() > 8)
        {
            IDR_ERROR("FrameBufferCreateInfo: 颜色附件数 {} 超过上限 8，已截断", color_formats.size());
            color_formats.resize(8);
        }
    }
} // namespace ID
