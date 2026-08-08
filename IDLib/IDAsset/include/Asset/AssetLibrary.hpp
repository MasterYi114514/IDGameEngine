#pragma once

#include "IDAssetCore.hpp"
#include "Asset/Asset.hpp"
#include "Asset/AssetPtr.hpp"
#include "Loader/TextureLoader.hpp"

#include <string>
#include <unordered_map>
#include <mutex>

namespace ID
{
    using TextureAsset = Asset<TextureData>;
    // using MeshAsset = Asset<MeshData>;

    class IDASSET_API AssetLibrary
    {
    public:
        AssetLibrary() = delete;
        ~AssetLibrary() = delete;

    public:
        // Texture 资源管理
        static AssetPtr<TextureData> load_texture(const std::string& path);
        static void reload_texture(AssetPtr<TextureData>& asset_ptr);
        static void reload_texture(const std::string& path);

    public:
        // 通用资产管理
        static bool is_loaded(const std::string& path);
        static void unload(const std::string& path);
        static void clear();
        static size_t size();

        // 统一使用正斜杠，去除重复斜杠
        static std::string normalize_path(const std::string& path);

    private:
        static std::unordered_map<std::string, TextureAsset> s_textures;
        static std::mutex s_texture_mutex;
    };
} // namespace ID