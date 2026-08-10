#pragma once

#include "IDAssetCore.hpp"
#include "Loader/IAssetLoader.hpp"

#include <vector>
#include <cstdint>

namespace ID
{
    struct IDASSET_API AudioData
    {
        std::vector<uint8_t> pcm_data;       // 解码后的 PCM 样本
        uint32_t             sample_rate = 0;
        uint8_t              channels    = 0;    // 1=单声道, 2=立体声
        uint8_t              bits_per_sample = 16;
        float                duration    = 0.0f;  // 秒

        bool is_valid() const { return !pcm_data.empty() && sample_rate > 0; }
    };

    template<>
    class IDASSET_API IAssetLoader<AudioData>
    {
    public:
        static Asset<AudioData> load(const std::string& path);
        static void reload(Asset<AudioData>& asset);
    };

    using AudioLoader = IAssetLoader<AudioData>;
} // namespace ID
