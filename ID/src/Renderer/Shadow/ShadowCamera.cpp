#include "Renderer/Shadow/ShadowCamera.hpp"
#include "Renderer/Shadow/ShadowConfig.hpp"
#include "Renderer/Camera/Camera.hpp"
#include "Log/Log.hpp"

#include <cmath>

namespace ID
{

    DirectionalShadowCamera::DirectionalShadowCamera(
        Camera* main_camera, float ortho_extent,
        float near_plane, float far_plane)
        : ShadowCamera(main_camera)
    {
        m_config.param.quality       = ShadowQuality::High;
        m_config.param.bias          = 0.0002f;
        m_config.param.normal_bias   = 0.02f;
        m_config.ortho_extent        = ortho_extent;
        m_config.near_plane          = near_plane;
        m_config.far_plane           = far_plane;
    }

    ShadowView DirectionalShadowCamera::compute_view(
        uint32_t /*index*/, const Camera& main_camera) const
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

        // 以主相机为锚点摆放正交视锥体
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

        return sv;
    }

} // namespace ID