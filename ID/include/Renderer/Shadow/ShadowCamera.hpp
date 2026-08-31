#pragma once

#include "IDpch.hpp"
#include "IDMath.hpp"
#include "Renderer/IDRCore.hpp"
#include "Renderer/Camera/Camera.hpp"
#include "Renderer/Shadow/ShadowConfig.hpp"
#include "Renderer/Render/RendererSettings.hpp"

namespace ID
{
    struct ShadowView
    {
        Mat4 view_proj = Math::get_identity_mat4();    // 光源视图投影矩阵
        Mat4 view = Math::get_identity_mat4();         // 光源视图矩阵
        Mat4 proj = Math::get_identity_mat4();         // 光源投影矩阵
        float far_bound = 0.0f;                        // 本层视锥远边界（视空间距离，正数；shader 选层用）
        float bias_scale = 1.0f;                       // 本层 AABB 宽度 / 层 0 宽度
    };

    class ShadowCamera
    {
    public:
        ShadowCamera(Camera* main_camera = nullptr) : m_main_camera(main_camera) { }
        virtual ~ShadowCamera() = default;

        void set_main_camera(Camera* main_camera) { m_main_camera = main_camera; }

        // 获取光源要渲染的视图数量
        virtual uint32_t get_view_count() const = 0;

        // 计算第 index 个视图的矩阵
        virtual ShadowView compute_view(uint32_t index, const Camera& main_camera) const = 0;

    protected:
        Camera* m_main_camera = nullptr;      // 主相机
    };

    class ID_API DirectionalShadowCamera : public ShadowCamera
    {
    public:
        DirectionalShadowCamera(Camera* main_camera = nullptr, float ortho_extent = 20.0f,
            float near_plane = 0.5f, float far_plane = 300.0f);

        virtual ~DirectionalShadowCamera() override = default;

        DirectionalShadowConfig& get_config() { return m_config; }
        const DirectionalShadowConfig& get_config() const { return m_config; }

        virtual uint32_t get_view_count() const override
        {
            // 层数唯一事实来源：RendererSettings::cascade_count（1 = 关闭 CSM）
            return std::clamp(get_renderer_settings().cascade_count, 1u, MAX_CASCADES);
        }
        virtual ShadowView compute_view(uint32_t index, const Camera& main_camera) const override;

        void set_direction(const Vec3& dir) { m_direction = dir; m_direction.normalize(); }
        const Vec3& get_direction() const { return m_direction; }

    private:
        Vec3 m_direction = Vec3(0.0f, -1.0f, 0.0f);    // 光源方向
        DirectionalShadowConfig m_config;

        // 层 0 AABB 宽度缓存（bias_scale 计算用；compute_view 必须按 0..count-1 顺序调用，ShadowPass 即如此）
        mutable float m_layer0_extent = 0.0f;
    };

    // TODO : PointShadowCamera / SpotShadowCamera
} // namespace ID