#include "Loader/IAssetLoader.hpp"
#include "Loader/TextureLoader.hpp"

#include "Log.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <filesystem>

namespace
{
    using namespace ID;
    TextureData load_texture_data(const std::string& path)
    {
        TextureData data;

        if(!std::filesystem::exists(path))
        {
            IDASSET_ERROR("TextureLoader::load：文件不存在: {}", path);
            return data;
        }

        // .hdr（Radiance RGBE）：float 像素，req_comp=4 强制 RGBA 交错（RGBA16F 内部格式 + GL_RGBA 数据格式要求 4 分量）
        if(stbi_is_hdr(path.c_str()))
        {
            float* raw_floats = stbi_loadf(path.c_str(), &data.width, &data.height, &data.channels, 4);
            if(!raw_floats)
            {
                IDASSET_ERROR("TextureLoader::load：加载 HDR 纹理失败: {}", path);
                return data;
            }
            data.is_float = true;
            data.pixels = PixPtr(reinterpret_cast<unsigned char*>(raw_floats), stbi_image_free);
            return data;
        }

        unsigned char* raw_pixels = stbi_load(path.c_str(), &data.width, &data.height, &data.channels, 0);
        if(!raw_pixels)
        {
            IDASSET_ERROR("TextureLoader::load：加载纹理失败: {}", path);
            return data;
        }

        data.pixels = PixPtr(raw_pixels, stbi_image_free);
        return data;
    }
} // 匿名命名空间

namespace ID
{
    Asset<TextureData> TextureLoader::load(const std::string& path)
    {
        TextureData data = load_texture_data(path);
        if(data.is_valid())
        {
            return Asset<TextureData>{AssetState::Loaded, std::move(data), path};
        }
        else
        {
            return Asset<TextureData>{AssetState::Failed, TextureData{}, path};
        }
    }

    void TextureLoader::reload(Asset<TextureData>& asset)
    {
        if(asset.path.empty())
        {
            IDASSET_ERROR("TextureLoader::reload：资源路径为空");
            asset.set_failed();
            return;
        }

        asset.reset();
        TextureData data = load_texture_data(asset.path);
        if(data.is_valid())
        {
            asset.data = std::move(data);
            asset.set_loaded();
        }
        else
        {
            asset.set_failed();
        }
    }
}