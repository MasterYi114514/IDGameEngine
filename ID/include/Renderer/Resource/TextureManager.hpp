#pragma once

#include "IDpch.hpp"
#include "Renderer/IDRCore.hpp"

namespace ID
{
    /**
     *  在 ID 内部，TextureManager 会覆盖全局的 TextureManager
     */
    class ID_API TextureManager
    {
    public:
        TextureManager() = delete;
        ~TextureManager() = delete;

    public:
        static TextureID load_texture(const std::string& path);
        static std::string get_texture_path(TextureID texture_id);
    };
} // namespace ID