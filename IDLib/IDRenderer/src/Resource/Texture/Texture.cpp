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
            case ID::TextureFormat::SRGB8_ALPHA8: return GL_SRGB8_ALPHA8;
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
            case ID::TextureFormat::SRGB8_ALPHA8: return GL_RGBA;
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
    Texture::Texture(const TextureCreateInfo& create_info)
        : m_width(create_info.width), m_height(create_info.height),
          m_layers(create_info.layers > 0 ? create_info.layers : 1)
    {
        // is_array 显式声明（ShadowMap：1 层亦走 array，保证 sampler2DArray 与纹理目标匹配）；layers > 1 自动 array
        m_target = (create_info.is_array || m_layers > 1) ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;

        glGenTextures(1, &m_texture_id);
        glBindTexture(m_target, m_texture_id);

        const bool is_depth = (create_info.format == TextureFormat::Depth);
        const GLenum data_type = create_info.format == TextureFormat::RGBA16F ? GL_FLOAT : GL_UNSIGNED_BYTE;

        if (m_target == GL_TEXTURE_2D_ARRAY)
        {
            // array 纹理：glTexImage3D 分配（CSM 逐层渲染；不做 mipmap）
            glTexImage3D
            (
                m_target, 0,
                static_cast<int>(to_gl_internal(create_info.format)),
                static_cast<int>(m_width),
                static_cast<int>(m_height),
                static_cast<int>(m_layers),
                0,
                to_gl_data_fmt(create_info.format),
                data_type,
                create_info.pixel_data
            );
        }
        else
        {
            glTexImage2D
            (
                m_target, 0,
                static_cast<int>(to_gl_internal(create_info.format)),
                static_cast<int>(m_width),
                static_cast<int>(m_height),
                0,
                to_gl_data_fmt(create_info.format),
                data_type,
                create_info.pixel_data
            );
        }

        if (is_depth)
        {
            // 深度纹理约定（与 FrameBuffer 深度附件一致）：NEAREST + CLAMP_TO_BORDER + 白 border（越界采样 = 1.0）
            glTexParameteri(m_target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(m_target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(m_target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
            glTexParameteri(m_target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
            float border_color[] = { 1.0f, 1.0f, 1.0f, 1.0f };
            glTexParameterfv(m_target, GL_TEXTURE_BORDER_COLOR, border_color);
        }
        else
        {
            glTexParameteri
            (
                m_target, GL_TEXTURE_MIN_FILTER,
                to_gl_filter(create_info.filter, create_info.gen_mips)
            );

            glTexParameteri
            (
                m_target, GL_TEXTURE_MAG_FILTER,
                create_info.filter == TextureFilter::Nearest ? GL_NEAREST : GL_LINEAR
            )
            ;
            glTexParameteri(m_target, GL_TEXTURE_WRAP_S, to_gl_wrap(create_info.wrap_s));

            glTexParameteri(m_target, GL_TEXTURE_WRAP_T, to_gl_wrap(create_info.wrap_t));

            // array 纹理不做 mipmap（gen_mips 对无数据纹理无效，array 由分层渲染填充）
            if (create_info.gen_mips && create_info.pixel_data && m_target == GL_TEXTURE_2D)
            {
                glGenerateMipmap(m_target);
            }
        }

        glBindTexture(m_target, 0);
    }

    void Texture::destroy()
    {
        if (m_texture_id != 0)
        {
            glDeleteTextures(1, &m_texture_id);
            m_texture_id = 0;
        }
    }

    Texture::Texture(Texture&& other)
        : m_width(other.m_width), m_height(other.m_height), m_layers(other.m_layers),
          m_target(other.m_target), m_texture_id(other.m_texture_id)
    {
        other.m_width      = 0;
        other.m_height     = 0;
        other.m_layers     = 1;
        other.m_target     = GL_TEXTURE_2D;
        other.m_texture_id = 0;
    }

    Texture& Texture::operator=(Texture&& other)
    {
        if (this != &other)
        {
            std::swap(m_width, other.m_width);
            std::swap(m_height, other.m_height);
            std::swap(m_layers, other.m_layers);
            std::swap(m_target, other.m_target);
            std::swap(m_texture_id, other.m_texture_id);

            other.destroy();
        }
        return *this;
    }
} // namespace ID

#endif