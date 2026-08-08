#include "Renderer/Resource/TextureManager.hpp"
#include "IDAsset.hpp"
#include "Log/Log.hpp"

namespace
{
    std::unordered_map<ID::TextureUINT, std::string> path_map;
} // 匿名命名空间

namespace ID
{
    TextureID TextureManager::load_texture(const std::string& path)
    {
        AssetPtr<TextureData> asset = AssetLibrary::load_texture(path);
        if(!asset.is_valid())
        {
            ID_ERROR("TextureManager::load_texture：加载纹理失败: {}", path);
            return TextureID::invalid_id();
        }

        TextureCreateInfo info
        { 
            static_cast<uint32_t>(asset->data.width), 
            static_cast<uint32_t>(asset->data.height), 
            asset->data.pixels.get() 
        };
        
        TextureID texture_id = ::TextureManager::create(info);

        path_map[texture_id.get_id()] = path;

        return texture_id;
    }

    std::string TextureManager::get_texture_path(TextureID texture_id)
    {
        auto it = path_map.find(texture_id.get_id());
        if(it != path_map.end())
        {
            return it->second;
        }
        else
        {
            ID_WARN("ID::TextureManager::get_texture_path：尝试查询不在 TextureManager 里创建的纹理");
            return "";
        }
    }
} // namespace ID