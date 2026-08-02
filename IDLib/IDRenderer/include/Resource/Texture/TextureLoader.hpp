#pragma once

#include "Core/IDRpch.hpp"

#include <filesystem>

namespace ID
{
    struct IDR_API TextureData
    {
        unsigned char* data   = nullptr;
        int            width  = 0;
        int            height = 0;

        TextureData() = default;
        ~TextureData();
    };

    class IDR_API TextureLoader
    {
    public:
        TextureLoader()  = delete;
        ~TextureLoader() = delete;

        static TextureData load_image(const std::filesystem::path& path);
    };
} // namespace ID
