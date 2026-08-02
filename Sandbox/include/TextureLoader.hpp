#pragma once

#include <string>
#include <filesystem>

#include "ID.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace ID
{
    struct TD
    {
        int width;
        int height;
        int channels;
        unsigned char* data;

        bool load(const std::filesystem::path& path)
        {
            stbi_set_flip_vertically_on_load(true);
            data = stbi_load(path.string().c_str(), &width, &height, &channels, 0);
            if(data == nullptr) return false;
            return true;
        }
    };
}