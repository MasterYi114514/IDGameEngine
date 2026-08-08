#pragma once

#include "IDpch.hpp"
#include "Renderer/Render/RenderPass/RenderPass.hpp"
#include "Renderer/Render/RenderContext.hpp"

namespace ID
{
    /**
     *  SkyboxPass：天空盒 Pass（Phase 4 新增）
     *
     *  渲染位置：所有不透明物体之后、透明物体之前（装配时置于 ForwardPass 与 TransparentPass 之间）
     *  技巧：
     *    - 用 MeshFactory::create_cube(2.0f)（顶点 ±1），VS 中 v_dir = aPos
     *    - gl_Position = proj * mat4(mat3(view)) * pos，再取 .xyww → 深度恒为 1.0（最远）
     *    - pipeline：depth_test = LessEqual + depth_write = false → 只填充"背景"像素，不污染深度
     *
     *  两种模式：
     *    - 程序化（默认）：FS 按视线方向插值顶/地平线/底色 + 太阳高光，零资产开箱即用
     *    - Cubemap（可选）：六面纹理采样（需要 IDRenderer cubemap 支持，见 README §4.3）
     */
    class ID_API SkyboxPass : public RenderPass
    {
    public:
        /**
         *  @param procedural    true = 程序化渐变天空（默认）；false = cubemap 纹理
         *  @param cubemap_dir   cubemap 目录（含 posx/negx/posy/negy/posz/negz.png 六面）
         */
        explicit SkyboxPass(bool procedural = true, const std::string& cubemap_dir = "");
        virtual ~SkyboxPass() override = default;

    public:
        // ── 程序化天空参数 ──
        void set_top_color(const Vec3& color) { m_top_color = color; }
        void set_horizon_color(const Vec3& color) { m_horizon_color = color; }
        void set_bottom_color(const Vec3& color) { m_bottom_color = color; }
        void set_sun_direction(const Vec3& dir) { m_sun_dir = dir; }
        void set_sun_intensity(float intensity) { m_sun_intensity = intensity; }

        // ── Cubemap 模式 ──
        void set_cubemap_dir(const std::string& dir) { m_cubemap_dir = dir; m_use_cubemap = !dir.empty(); }
        bool is_cubemap_mode() const { return m_use_cubemap; }

    public:
        virtual void execute(RenderContext& ctx) override;

    private:
        // 懒创建：skybox shader + cube mesh + pipeline（+ 可选 cubemap）
        void ensure_resources();
        
        // 加载六面 cubemap（依赖 IDRenderer cubemap 增强）
        bool load_cubemap(const std::string& dir);

    private:
        bool        m_use_cubemap = false;      // false = 程序化
        std::string m_cubemap_dir;

        ShaderID    m_shader   = ShaderID::invalid_id();
        MeshID      m_cube     = MeshID::invalid_id();
        PipelineID  m_pipeline = PipelineID::invalid_id();
        TextureID   m_cubemap  = TextureID::invalid_id();

        // 程序化天空默认配色：偏蓝白昼
        Vec3 m_top_color      = Vec3(0.25f, 0.45f, 0.85f);
        Vec3 m_horizon_color  = Vec3(0.70f, 0.80f, 0.95f);
        Vec3 m_bottom_color   = Vec3(0.45f, 0.35f, 0.30f);
        Vec3 m_sun_dir        = Vec3(0.30f, 0.70f, -0.50f);   // 默认与主光源方向一致
        float m_sun_intensity = 1.0f;
    };
} // namespace ID