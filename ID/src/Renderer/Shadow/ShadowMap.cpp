#include "Renderer/Shadow/ShadowMap.hpp"

#include "Log/Log.hpp"

namespace ID
{
    ShadowMap::ShadowMap(uint32_t size, uint32_t layer_count)
        : m_layer_count(layer_count > 0 ? layer_count : 1), m_size(size)
    {
        // 深度 array 纹理：Texture 构造对 Depth 格式强制 NEAREST + CLAMP_TO_BORDER + 白 border（越界 = 1.0）
        TextureCreateInfo tex_info(size, size, nullptr, TextureFormat::Depth);
        tex_info.layers   = m_layer_count;
        tex_info.is_array = true;   // ⚠ 1 层亦走 GL_TEXTURE_2D_ARRAY：shader 以 sampler2DArray 采样，目标不匹配 = 未定义行为（阴影全亮/全暗）
        m_depth_array = TextureManager::create(tex_info);
        if (!m_depth_array.is_valid())
        {
            ID_ERROR("[ShadowMap] 深度 array 纹理创建失败");
            return;
        }

        // 渲染 FBO：无颜色附件（纯深度形态）、无自带深度——深度 = 外部 array 纹理逐层 attach
        FrameBufferCreateInfo fb_info(size, size, std::vector<TextureFormat>{});
        fb_info.has_depth_attachment = false;
        m_fb = FBManager::create(fb_info);
        if (!m_fb.is_valid())
        {
            ID_ERROR("[ShadowMap] FBO 创建失败");
            return;
        }

        // 创建后立即 attach 层 0，保证 FBO 始终完整（ShadowPass 渲染前会重新 attach 目标层）
        IDRCmd::attach_framebuffer_depth_layer(m_fb, m_depth_array, 0);
    }

    ShadowMap::ShadowMap(ShadowMap&& other) noexcept
        : m_depth_array(other.m_depth_array), m_fb(other.m_fb),
          m_layer_count(other.m_layer_count), m_size(other.m_size),
          m_type(other.m_type)
    {
        other.m_depth_array = TextureID::invalid_id();
        other.m_fb          = FrameBufferID::invalid_id();
        other.m_layer_count = 1;
        other.m_size        = 0;
    }

    ShadowMap& ShadowMap::operator=(ShadowMap&& other) noexcept
    {
        if (this != &other)
        {
            std::swap(m_depth_array, other.m_depth_array);
            std::swap(m_fb, other.m_fb);
            std::swap(m_layer_count, other.m_layer_count);
            std::swap(m_size, other.m_size);
            std::swap(m_type, other.m_type);

            other.destroy();
        }
        return *this;
    }

    void ShadowMap::destroy()
    {
        // 生命周期：先 FBO 后纹理（FBO attach 着外部 array 纹理，顺序反了会产生悬空附件）
        if (m_fb.is_valid())
        {
            FBManager::destroy(m_fb);
            m_fb = FrameBufferID::invalid_id();
        }
        if (m_depth_array.is_valid())
        {
            TextureManager::destroy(m_depth_array);
            m_depth_array = TextureID::invalid_id();
        }
        m_layer_count = 1;
        m_size        = 0;
    }
} // namespace ID
