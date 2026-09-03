#include "Renderer/Shadow/ShadowCamera.hpp"
#include "Renderer/Shadow/ShadowConfig.hpp"
#include "Renderer/Camera/Camera.hpp"
#include "Log/Log.hpp"

#include <cmath>
#include <limits>

namespace ID
{

    DirectionalShadowCamera::DirectionalShadowCamera(
        Camera* main_camera, float ortho_extent,
        float near_plane, float far_plane)
        : ShadowCamera(main_camera)
    {
        // ShadowParam（quality / bias / normal_bias）以 ShadowConfig.hpp 结构体默认值为
        // 唯一事实来源，构造函数不再写死覆盖（曾导致调试时改默认值不生效）
        m_config.ortho_extent        = ortho_extent;
        m_config.near_plane          = near_plane;
        m_config.far_plane           = far_plane;
    }

    ShadowView DirectionalShadowCamera::compute_view(
        uint32_t index, const Camera& main_camera) const
    {
        Vec3 dir = m_direction;
        if (dir.is_zero())
        {
            ID_WARN("[DirectionalShadowCamera] 光源方向为零向量，返回单位矩阵");
            return ShadowView{};
        }
        dir.normalize();

        // up 退化处理
        Vec3 up(0.0f, 1.0f, 0.0f);
        if (std::abs(up.dot(dir)) > 0.99f)
            up = Vec3(1.0f, 0.0f, 0.0f);

        const RendererSettings& settings = get_renderer_settings();
        const uint32_t count = std::clamp(settings.cascade_count, 1u, MAX_CASCADES);
        if (index >= count)
        {
            ID_WARN("[DirectionalShadowCamera] index {} 超出级联层数 {}，返回单位矩阵", index, count);
            return ShadowView{};
        }

        // 单级联（CSM 关闭）：保持旧行为——以相机为锚点、ortho_extent 固定视锥
        // （回退铁律：cascade_count=1 时画面必须与基线逐像素一致；8 角点包围盒方案
        //  会让贴图覆盖范围暴涨到相机 far 平面，texel 密度崩盘 + IGN 抖动图案可见）
        if (count == 1)
        {
            const Pos3 cam_pos = main_camera.get_pose().position;
            const Pos3 eye = cam_pos
                - dir * (m_config.ortho_extent + m_config.near_plane);

            ShadowView sv;
            sv.view      = Math::get_look_at(eye, eye + dir, up);
            sv.proj      = Math::get_orthographic(
                -m_config.ortho_extent, m_config.ortho_extent,
                -m_config.ortho_extent, m_config.ortho_extent,
                m_config.near_plane, m_config.far_plane);
            sv.view_proj = sv.proj * sv.view;
            sv.far_bound = 0.0f;   // 单级联不选层（shader 端 u_cascade_count <= 1 时固定层 0）
            sv.bias_scale = 1.0f;  // 单级联无 texel 尺寸维度缩放（并复位层 0 缓存，防 CSM 关闭后残留）
            m_layer0_extent = 0.0f;

            return sv;
        }

        // PSSM 分割（count > 1）
        const ProjectionParams& proj_params = main_camera.get_projection();
        const float near_z = proj_params.near_z;
        const float far_z  = (settings.cascade_far_override > 0.0f)
            ? settings.cascade_far_override : proj_params.far_z;

        // split(i) 即第i层的远平面
        auto split = [&](uint32_t i) -> float
        {
            const float t = static_cast<float>(i + 1) / static_cast<float>(count);
            const float uniform  = near_z + (far_z - near_z) * t;
            const float logarith = near_z * std::pow(far_z / near_z, t);

            const float lambda = settings.cascade_lambda;
            const float oml = 1.0f - lambda;    // 1 - λ

            return uniform * oml + logarith * lambda;
        };

        const float layer_near = (index == 0) ? near_z : split(index - 1);
        const float layer_far  = split(index);

        // 主相机视锥第 index 段 8 角点（视空间；OpenGL 右手系，相机看向 -z）
        const float tan_half_fov = std::tan(Math::radians(proj_params.persp.fov_y) * 0.5f);
        const float y_near = tan_half_fov * layer_near;
        const float x_near = y_near * proj_params.persp.aspect;
        const float y_far  = tan_half_fov * layer_far;
        const float x_far  = y_far * proj_params.persp.aspect;

        const Vec3 corners_view[8] = {
            Vec3(-x_near, -y_near, -layer_near), Vec3(x_near, -y_near, -layer_near),
            Vec3(-x_near,  y_near, -layer_near), Vec3(x_near,  y_near, -layer_near),
            Vec3(-x_far,  -y_far,  -layer_far),  Vec3(x_far,  -y_far,  -layer_far),
            Vec3(-x_far,   y_far,  -layer_far),  Vec3(x_far,   y_far,  -layer_far),
        };

        // 视空间 → 世界（逆 view 矩阵）
        const Mat4 inv_view = Math::get_inverse(main_camera.get_view_matrix());

        // 光源视图（以主相机为锚点；方向不变）
        const Pos3 cam_pos = main_camera.get_pose().position;
        const Mat4 light_view = Math::get_look_at(cam_pos, cam_pos + dir, up);

        // 8 角点投影到光源空间，求 AABB（正交投影范围）
        float min_x = std::numeric_limits<float>::max();
        float max_x = -std::numeric_limits<float>::max();
        float min_y = std::numeric_limits<float>::max();
        float max_y = -std::numeric_limits<float>::max();
        float min_z = std::numeric_limits<float>::max();
        float max_z = -std::numeric_limits<float>::max();

        for(const Vec3& c : corners_view)
        {
            const Vec4 world = inv_view * Vec4(c, 1.0f);
            const Vec4 light = light_view * world;

            min_x = std::min(min_x, light[0]);
            max_x = std::max(max_x, light[0]);
            min_y = std::min(min_y, light[1]);
            max_y = std::max(max_y, light[1]);
            min_z = std::min(min_z, light[2]);
            max_z = std::max(max_z, light[2]);
        }

        // 正交投影：光源看向 -z，近远平面外扩 1 世界单位防边缘裁剪
        constexpr float z_pad = 1.0f;
        const float proj_near = -(max_z + z_pad);
        const float proj_far  = -(min_z - z_pad);
        if (proj_far <= proj_near)
        {
            ID_WARN("[DirectionalShadowCamera] 级联 {} 正交近远平面异常 (near={} far={})，返回单位矩阵",
                index, proj_near, proj_far);
            return ShadowView{};
        }

        // bias_scale：本层 AABB 宽度 / 层 0 宽度（texel 尺寸维度；远层 texel 世界尺寸更大，
        // 需等比放大 bias 抗斜条纹；层 0 宽度由首次调用缓存，要求按 0..count-1 顺序调用）
        const float aabb_width = max_x - min_x;
        float bias_scale = 1.0f;
        if (index == 0)
        {
            m_layer0_extent = aabb_width;
        }
        else if (m_layer0_extent > 0.0f)
        {
            bias_scale = aabb_width / m_layer0_extent;
        }

        ShadowView sv;
        sv.view      = light_view;
        sv.proj      = Math::get_orthographic(min_x, max_x, min_y, max_y, proj_near, proj_far);
        sv.view_proj = sv.proj * sv.view;
        sv.far_bound = layer_far;   // 视空间距离（正数），shader 选层用（u_cascade_splits）
        sv.bias_scale = bias_scale; // texel 尺寸维度 bias 缩放（Step 8.5）

        return sv;
    }

} // namespace ID