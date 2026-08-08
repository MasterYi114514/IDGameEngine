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
        PixPtr  pixels      = { nullptr, nullptr };  

        bool is_valid() const { return pixels && width > 0 && height > 0; }
        size_t byte_size() const { return pixels ? width * height * channels : 0; }
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