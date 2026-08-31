#include "Renderer/Resource/TextureManager.hpp"
#include "IDAsset.hpp"
#include "Log/Log.hpp"

namespace
{
    std::unordered_map<ID::TextureUINT, std::string> path_map;
} // 匿名命名空间

namespace ID
{
    TextureID TextureManager::load_texture(const std::string& path, bool srgb)
    {
        AssetPtr<TextureData> asset = AssetLibrary::load_texture(path);
        if(!asset.is_valid())
        {
            ID_ERROR("TextureManager::load_texture：加载纹理失败: {}", path);
            return TextureID::invalid_id();
        }

        // 格式选择：float 像素（.hdr）→ RGBA16F（数据本就线性，忽略 srgb 解码）；
        // byte 像素：srgb=true → SRGB8_ALPHA8（采样时硬件解码到线性域，albedo 语义），
        //            srgb=false → RGBA8（预览用途，ImGui 直接显示解码后值会偏暗）
        const ID::TextureFormat fmt = asset->data.is_float ? ID::TextureFormat::RGBA16F
                                    : (srgb ? ID::TextureFormat::SRGB8_ALPHA8
                                            : ID::TextureFormat::RGBA8);
        TextureCreateInfo info
        { 
            static_cast<uint32_t>(asset->data.width), 
            static_cast<uint32_t>(asset->data.height), 
            asset->data.pixels.get(),
            fmt
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