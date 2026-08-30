#include "Resource/Sampler/Sampler.hpp"

#ifdef IDRENDERER_USE_OPENGL

namespace
{
    GLenum to_gl_sampler_filter(ID::TextureFilter filter)
    {
        switch (filter)
        {
            case ID::TextureFilter::Nearest:    return GL_NEAREST;
            case ID::TextureFilter::Linear:     return GL_LINEAR;
            case ID::TextureFilter::Trilinear:  return GL_LINEAR;   // 阴影贴图无 mipmap，退化为 LINEAR
            default:                            return GL_LINEAR;
        }
    }

    GLenum to_gl_sampler_wrap(ID::TextureWrap wrap)
    {
        switch (wrap)
        {
            case ID::TextureWrap::Repeat:        return GL_REPEAT;
            case ID::TextureWrap::ClampToEdge:   return GL_CLAMP_TO_EDGE;
            case ID::TextureWrap::ClampToBorder: return GL_CLAMP_TO_BORDER;
            default:                             return GL_REPEAT;
        }
    }
} // 匿名命名空间

namespace ID
{
    Sampler::Sampler(const SamplerCreateInfo& create_info)
    {
        glGenSamplers(1, &m_sampler);
        glSamplerParameteri(m_sampler, GL_TEXTURE_MIN_FILTER, to_gl_sampler_filter(create_info.filter_min));
        glSamplerParameteri(m_sampler, GL_TEXTURE_MAG_FILTER, to_gl_sampler_filter(create_info.filter_mag));
        glSamplerParameteri(m_sampler, GL_TEXTURE_WRAP_S, to_gl_sampler_wrap(create_info.wrap_s));
        glSamplerParameteri(m_sampler, GL_TEXTURE_WRAP_T, to_gl_sampler_wrap(create_info.wrap_t));
        glSamplerParameterfv(m_sampler, GL_TEXTURE_BORDER_COLOR, create_info.border_color);

        // 深度比较模式（硬件 PCF 核心）：cmp 采样器 → GL_COMPARE_REF_TO_TEXTURE + GL_LESS
        const bool compare = (create_info.compare == TextureCompare::RefToTexture);
        glSamplerParameteri(m_sampler, GL_TEXTURE_COMPARE_MODE,
            compare ? GL_COMPARE_REF_TO_TEXTURE : GL_NONE);
        glSamplerParameteri(m_sampler, GL_TEXTURE_COMPARE_FUNC, GL_LESS);
    }

    void Sampler::destroy()
    {
        if (m_sampler != 0)
        {
            glDeleteSamplers(1, &m_sampler);
            m_sampler = 0;
        }
    }

    Sampler::Sampler(Sampler&& other) noexcept : m_sampler(other.m_sampler)
    {
        other.m_sampler = 0;   // 剥夺源对象所有权
    }

    Sampler& Sampler::operator=(Sampler&& other) noexcept
    {
        if (this != &other)
        {
            std::swap(m_sampler, other.m_sampler);
            other.destroy();
        }
        return *this;
    }
} // namespace ID

#endif // IDRENDERER_USE_OPENGL
