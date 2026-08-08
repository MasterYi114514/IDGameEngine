#include "Asset/AssetLibrary.hpp"
#include "Loader/TextureLoader.hpp"
#include "Log.hpp"

#include <filesystem>
#include <algorithm>

namespace ID
{
    std::unordered_map<std::string, Asset<TextureData>> AssetLibrary::s_textures;
    std::mutex AssetLibrary::s_texture_mutex;

    std::string AssetLibrary::normalize_path(const std::string& path)
    {
        std::filesystem::path p(path);
        std::string normalized = p.lexically_normal().generic_string();
        return normalized;
    }

    AssetPtr<TextureData> AssetLibrary::load_texture(const std::string& path)
    {
        std::string key = normalize_path(path);
        std::lock_guard<std::mutex> lock(s_texture_mutex);

        auto it = s_textures.find(key);
        if (it != s_textures.end() && it->second.is_loaded())
        {
            return AssetPtr<TextureData>(&it->second);
        }

        Asset<TextureData> asset = TextureLoader::load(path);

        if(asset.is_loaded())
        {
            std::filesystem::path fs_path(path);
            asset.name = fs_path.stem().string();
            asset.path = key;

            s_textures[key] = std::move(asset);
            return AssetPtr<TextureData>(&s_textures[key]);
        }
        else
        {
            IDASSET_ERROR("AssetLibrary::load_texture：加载纹理失败: {}", key);
            return AssetPtr<TextureData>(nullptr);
        }
    }

    void AssetLibrary::reload_texture(AssetPtr<TextureData>& asset_ptr)
    {
        if (!asset_ptr.is_valid())
        {
            IDASSET_ERROR("AssetLibrary::reload_texture：无效的纹理资源指针");
            return;
        }

        std::lock_guard<std::mutex> lock(s_texture_mutex);

        std::string key = normalize_path(asset_ptr->path);
        auto it = s_textures.find(key);
        if(it == s_textures.end())
        {
            IDASSET_ERROR("AssetLibrary::reload_texture：纹理资源未找到: {}", key);
            return;
        }

        it->second.reset();

        Asset<TextureData> asset = TextureLoader::load(key);
        asset.name = it->second.name;       // 保留原来的名称

        if(asset.is_loaded())
        {
            it->second = std::move(asset);
        }
    }

    void AssetLibrary::reload_texture(const std::string& path)
    {
        std::string key = normalize_path(path);
        std::lock_guard<std::mutex> lock(s_texture_mutex);

        auto it = s_textures.find(key);
        if(it == s_textures.end())
        {
            IDASSET_ERROR("AssetLibrary::reload_texture：纹理资源未找到: {}", key);
            return;
        }

        it->second.reset();

        Asset<TextureData> asset = TextureLoader::load(key);
        asset.name = it->second.name;       // 保留原来的名称

        if(asset.is_loaded())
        {
            it->second = std::move(asset);
        }
    }

    bool AssetLibrary::is_loaded(const std::string& path)
    {
        std::string key = normalize_path(path);
        std::lock_guard<std::mutex> lock(s_texture_mutex);

        auto it = s_textures.find(key);
        return it != s_textures.end() && it->second.is_loaded();
    }

    void AssetLibrary::unload(const std::string& path)
    {
        std::string key = normalize_path(path);
        std::lock_guard<std::mutex> lock(s_texture_mutex);

        auto it = s_textures.find(key);
        if(it != s_textures.end())
        {
            s_textures.erase(it);
        }
    }

    void AssetLibrary::clear()
    {
        std::lock_guard<std::mutex> lock(s_texture_mutex);
        s_textures.clear();
    }

    size_t AssetLibrary::size()
    {
        std::lock_guard<std::mutex> lock(s_texture_mutex);
        return s_textures.size();
    }
} // namespace ID