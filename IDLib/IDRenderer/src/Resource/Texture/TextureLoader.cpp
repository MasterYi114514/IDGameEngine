#include "Resource/Texture/TextureLoader.hpp"
#include "Log/Log.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "Resource/Texture/stb_image.h"

namespace ID
{
    TextureData::~TextureData()
    {
        if (data)
        {
            stbi_image_free(data);
            data = nullptr;
        }
    }

    TextureData TextureLoader::load_image(const std::filesystem::path& path)
    {
        // 设置stb_image的垂直翻转选项
        static bool is_set = true;
        if(is_set)
        {
            stbi_set_flip_vertically_on_load(true);
            is_set = false;
        }

        TextureData data;
        data.data = stbi_load(path.string().c_str(), &data.width, &data.height, nullptr, 0);
        
        if(data.data == nullptr)
        {
            IDR_ERROR("加载路径为 {} 的纹理失败，原因为 {}", path.string(), stbi_failure_reason());
        }

        return data;
    }
} // namespace ID
