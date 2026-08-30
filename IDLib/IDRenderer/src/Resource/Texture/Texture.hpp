#pragma once

#include "Core/IDRpch.hpp"
#include "Resource/Texture/TextureCreateInfo.hpp"

#ifdef IDRENDERER_USE_OPENGL

#include <glad/glad.h>

namespace ID
{
    class Texture
    {
    public:
        Texture(const TextureCreateInfo& createInfo);
        ~Texture() { destroy(); }

        // 禁止拷贝
        Texture(const Texture&)            = delete;
        Texture& operator=(const Texture&) = delete;

        // 允许移动
        Texture(Texture&&);
        Texture& operator=(Texture&&);

        void destroy();

    public:
        uint32_t get_width()        const { return m_width;  }
        uint32_t get_height()       const { return m_height; }
        uint32_t get_layers()       const { return m_layers; }
        uint32_t get_texture_id()   const { return m_texture_id; }

        // 纹理目标（GL_TEXTURE_2D / GL_TEXTURE_2D_ARRAY）；绑定/解绑按此目标进行
        GLenum   get_target()       const { return m_target; }

    private:
        uint32_t m_width        = 0;
        uint32_t m_height       = 0;
        uint32_t m_layers       = 1;
        GLenum   m_target       = GL_TEXTURE_2D;
        GLuint   m_texture_id   = 0;
    };
} // namespace ID

#endif