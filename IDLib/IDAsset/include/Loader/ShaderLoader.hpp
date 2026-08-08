#pragma once

#include "IDAssetCore.hpp"
#include "Asset/Asset.hpp"
#include "Loader/IAssetLoader.hpp"

#include <string>

namespace ID
{
    struct ShaderData
    {
        std::string vertex_source;
        std::string fragment_source;
        std::string vs_path;
        std::string fs_path;

        bool is_valid() const { return !vertex_source.empty() && !fragment_source.empty(); }
    };

    template<>
    class IDASSET_API IAssetLoader<ShaderData>
    {
    public:
        Asset<ShaderData> load(const std::string& vs_path, const std::string& fs_path);
        void reload(Asset<ShaderData>& asset);
    };

    using ShaderLoader = IAssetLoader<ShaderData>;
} // namespace ID