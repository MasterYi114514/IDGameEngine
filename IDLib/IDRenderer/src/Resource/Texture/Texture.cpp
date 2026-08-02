#include "Resource/Texture/Texture.hpp"

#ifdef IDRENDERER_USE_OPENGL

namespace
{
    uint32_t to_gl_internal(ID::TextureFormat fmt)
    {
        switch (fmt)
        {
            case ID::TextureFormat::R8:         return GL_R8;
            case ID::TextureFormat::RG8:        return GL_RG8;
            case ID::TextureFormat::RGB8:       return GL_RGB8;
            case ID::TextureFormat::RGBA8:      return GL_RGBA8;
            case ID::TextureFormat::RGBA16F:    return GL_RGBA16F;
            case ID::TextureFormat::Depth:      return GL_DEPTH_COMPONENT24;
            default:                            return GL_RGBA8;
        }
    }

    uint32_t to_gl_data_fmt(ID::TextureFormat fmt)
    {
        switch (fmt)
        {
            case ID::TextureFormat::R8:         return GL_RED;
            case ID::TextureFormat::RG8:        return GL_RG;
            case ID::TextureFormat::RGB8:       return GL_RGB;
            case ID::TextureFormat::RGBA8:      return GL_RGBA;
            case ID::TextureFormat::RGBA16F:    return GL_RGBA;
            case ID::TextureFormat::Depth:      return GL_DEPTH_COMPONENT;
            default:                            return GL_RGBA;
        }
    }

    uint32_t to_gl_filter(ID::TextureFilter filter, bool has_mips)
    {
        switch (filter)
        {
            case ID::TextureFilter::Nearest:    return GL_NEAREST;
            case ID::TextureFilter::Linear:     return has_mips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR;
            case ID::TextureFilter::Trilinear:  return GL_LINEAR_MIPMAP_LINEAR;
            default:                            return GL_LINEAR;
        }
    }

    uint32_t to_gl_wrap(ID::TextureWrap wrap)
    {
        switch (wrap)
        {
            case ID::TextureWrap::Repeat:           return GL_REPEAT;
            case ID::TextureWrap::ClampToEdge:      return GL_CLAMP_TO_EDGE;
            case ID::TextureWrap::ClampToBorder:    return GL_CLAMP_TO_BORDER;
            default:                                return GL_REPEAT;
        }
    }
} // 匿名命名空间

namespace ID
{
    Texture::Texture(const TextureCreateInfo& create_info) : m_width(create_info.width), m_height(create_info.height)
    {
        glGenTextures(1, &m_texture_id);
        glBindTexture(GL_TEXTURE_2D, m_texture_id);

        glTexImage2D
        (
            GL_TEXTURE_2D, 0, 
            static_cast<int>(to_gl_internal(create_info.format)),
            static_cast<int>(m_width), 
            static_cast<int>(m_height), 
            0, 
            to_gl_data_fmt(create_info.format),
            create_info.format == TextureFormat::RGBA16F ? GL_FLOAT : GL_UNSIGNED_BYTE, 
            create_info.pixel_data
        );

        glTexParameteri
        (
            GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
            to_gl_filter(create_info.filter, create_info.gen_mips)
        );

        glTexParameteri
        (
            GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
            create_info.filter == TextureFilter::Nearest ? GL_NEAREST : GL_LINEAR
        )
        ;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, to_gl_wrap(create_info.wrap_s));

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, to_gl_wrap(create_info.wrap_t));

        if (create_info.gen_mips && create_info.pixel_data)
        {
            glGenerateMipmap(GL_TEXTURE_2D);
        }

        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void Texture::destroy()
    {
        if (m_texture_id != 0)
        {
            glDeleteTextures(1, &m_texture_id);
            m_texture_id = 0;
        }
    }

    Texture::Texture(Texture&& other) : m_width(other.m_width), m_height(other.m_height), m_texture_id(other.m_texture_id)
    {
        other.m_width = 0;
        other.m_height = 0;
        other.m_texture_id = 0;
    }

    Texture& Texture::operator=(Texture&& other)
    {
        if (this != &other)
        {
            std::swap(m_width, other.m_width);
            std::swap(m_height, other.m_height);
            std::swap(m_texture_id, other.m_texture_id);

            other.destroy();
        }
        return *this;
    }
} // namespace ID

#endif