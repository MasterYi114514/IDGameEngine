#pragma once

#include "IDAssetCore.hpp"
#include "Loader/IAssetLoader.hpp"

#include <vector>
#include <memory>

namespace ID
{
    using PixPtr = std::unique_ptr<unsigned char, void(*)(void*)>;  // 数据指针，delete 函数

    struct IDASSET_API TextureData
    {
        int     width       = 0;
        int     height      = 0;
        int     channels    = 0;
        bool    is_float    = false;                     // true = float 像素（.hdr；元素为 float 而非 byte）
        PixPtr  pixels      = { nullptr, nullptr };      // 字节容器语义；is_float 时按 float 解释

        bool is_valid() const { return pixels && width > 0 && height > 0; }
        size_t byte_size() const
        {
            return pixels ? size_t(width) * size_t(height) * size_t(channels) * (is_float ? 4 : 1) : 0;
        }
    };

    // 特化
    template<>
    class IDASSET_API IAssetLoader<TextureData>
    {
    public:
        static Asset<TextureData> load(const std::string& path);
        static void reload(Asset<TextureData>& asset);
    };

    using TextureLoader = IAssetLoader<TextureData>;
} // namespace ID