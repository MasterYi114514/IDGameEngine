#pragma once

#include "IDpch.hpp"
#include "Renderer/IDRCore.hpp"
#include "Renderer/Shadow/ShadowConfig.hpp"
#include "Renderer/Shadow/ShadowCamera.hpp"

namespace ID
{
    enum class ShadowMapType : uint8_t
    {
        Texture2D = 0,    // 2D 阴影贴图（平行光 / 聚光灯）
        CubeMap   = 1,    // Cube 阴影贴图（点光源）
    };

    /*
    *   ShadowMap — 阴影贴图（v2：单张 Texture2DArray 深度纹理 + 单 FBO 逐层 attach）
    *
    *   layer_count = 1 亦走 array 路径（统一形态；cascade_count=1 只用层 0）。
    *
    *   生命周期：析构先 destroy FBO 再 destroy 纹理 —— FBO 通过
    *   attach_framebuffer_depth_layer 引用外部 array 纹理，顺序反了会在
    *   GL 名字回收机制下产生悬空附件（未定义采样行为）。
    */
    class ID_API ShadowMap
    {
    public:
        ShadowMap() = default;
        explicit ShadowMap(uint32_t size, uint32_t layer_count = 1);
        ~ShadowMap() { destroy(); }

        ShadowMap(const ShadowMap&) = delete;
        ShadowMap& operator=(const ShadowMap&) = delete;
        ShadowMap(ShadowMap&& other) noexcept;
        ShadowMap& operator=(ShadowMap&& other) noexcept;

        // 先 FBO 后纹理（见类注释）
        void destroy();

        bool           is_valid()       const { return m_depth_array.is_valid(); }

        TextureID      get_depth_array() const { return m_depth_array; }   // GL_TEXTURE_2D_ARRAY 深度纹理
        FrameBufferID  get_fb()         const { return m_fb; }             // 渲染用 FBO（无颜色附件、无自带深度）
        uint32_t       get_layer_count() const { return m_layer_count; }
        uint32_t       get_size()       const { return m_size; }

        ShadowMapType  get_type()       const { return m_type; }
        void           set_type(ShadowMapType type) { m_type = type; }

    private:
        TextureID      m_depth_array = TextureID::invalid_id();    // array 深度纹理（采样绑定用）
        FrameBufferID  m_fb          = FrameBufferID::invalid_id();// 渲染 FBO（逐层 attach 深度）
        uint32_t       m_layer_count = 1;
        uint32_t       m_size        = 0;
        ShadowMapType  m_type        = ShadowMapType::Texture2D;
    };
} // namespace ID
