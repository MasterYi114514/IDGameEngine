#pragma once

#include "Asset/Asset.hpp"

namespace ID
{
    template<typename T>
    struct IAssetLoader
    {
        static Asset<T> load(const std::string& path);
        static void reload(Asset<T>& asset);
    };
}