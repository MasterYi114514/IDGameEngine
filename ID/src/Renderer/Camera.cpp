#include "Renderer/Camera/Camera.hpp"
#include "Renderer/Camera/CameraController.hpp"

namespace ID
{
    // =====================================================================
    //  pose 操作
    // =====================================================================

    void Camera::set_pose(const CameraPose& pose)
    {
        m_pose = pose;
        is_view_dirty = true;
    }

    void Camera::set_position(const Pos3& position)
    {
        m_pose.position = position;
        is_view_dirty = true;
    }

    void Camera::look_at(const Pos3& target, const Vec3& up)
    {
        Vec3 front = target - m_pose.position;
        front.normalize();
        set_orientation(front, up);
    }

    void Camera::set_orientation(const Vec3& front, const Vec3& up)
    {
        m_pose.front = front;
        m_pose.up    = up;
        is_view_dirty = true;
    }

    // =====================================================================
    //  projection 操作
    // =====================================================================

    void Camera::set_projection(const ProjectionParams& projection)
    {
        m_projection = projection;
        is_projection_dirty = true;
    }

    void Camera::set_projection_type(ProjectionType type)
    {
        m_projection.type = type;
        is_projection_dirty = true;
    }

    void Camera::set_orthographic(float left, float right,
                                   float top, float bottom,
                                   float near_plane, float far_plane)
    {
        m_projection.type            = ProjectionType::Orthographic;
        m_projection.ortho.left      = left;
        m_projection.ortho.right     = right;
        m_projection.ortho.top       = top;
        m_projection.ortho.bottom    = bottom;
        m_projection.near_z          = near_plane;
        m_projection.far_z           = far_plane;
        is_projection_dirty = true;
    }

    void Camera::set_perspective(float fov_y, float aspect)
    {
        m_projection.type        = ProjectionType::Perspective;
        m_projection.persp.fov_y = fov_y;
        m_projection.persp.aspect = aspect;
        is_projection_dirty = true;
    }

    void Camera::set_viewport_size(float width, float height)
    {
        float aspect = (height != 0.0f) ? (width / height) : 1.0f;

        switch (m_projection.type)
        {
        case ProjectionType::Perspective:
            m_projection.persp.aspect = aspect;
            break;
        case ProjectionType::Orthographic:
        {
            float half_h = m_projection.ortho.top;
            float half_w = half_h * aspect;
            m_projection.ortho.left   = -half_w;
            m_projection.ortho.right  =  half_w;
            break;
        }
        }
        is_projection_dirty = true;
    }

    void Camera::update_view_matrix() const
    {
        if(!is_view_dirty) return;

        m_view_matrix = Math::get_look_at(
            m_pose.position,
            m_pose.position + m_pose.front,
            m_pose.up
        );
        is_view_dirty = false;
    }

    void Camera::update_projection_matrix() const
    {
        if(!is_projection_dirty) return;

        switch (m_projection.type)
        {
        case ProjectionType::Perspective:
            m_projection_matrix = Math::get_perspective(
                m_projection.persp.fov_y,
                m_projection.persp.aspect,
                m_projection.near_z,
                m_projection.far_z
            );
            break;

        case ProjectionType::Orthographic:
            m_projection_matrix = Math::get_orthographic(
                m_projection.ortho.left,
                m_projection.ortho.right,
                m_projection.ortho.bottom,
                m_projection.ortho.top,
                m_projection.near_z,
                m_projection.far_z
            );
            break;
        }
        is_projection_dirty = false;
    }

    void Camera::update_frustum() const
    {
        if(is_view_dirty || is_projection_dirty)
        {
            update_view_matrix();
            update_projection_matrix();
        }

        // VP = projection * view
        Mat4 vp = m_projection_matrix * m_view_matrix;

        // Gribb/Hartmann 视锥体平面提取
        // 对于列优先矩阵，行 i = (vp[i][0], vp[i][1], vp[i][2], vp[i][3])
        // 平面方程: a*x + b*y + c*z + d = 0，法向量指向视锥体内部

        auto extract_plane = [&vp](int row_a, int row_b, float sign) -> FrustumPlane
        {
            Vec3 normal(
                vp[row_a][0] + sign * vp[row_b][0],
                vp[row_a][1] + sign * vp[row_b][1],
                vp[row_a][2] + sign * vp[row_b][2]
            );
            float d = vp[row_a][3] + sign * vp[row_b][3];

            float len = normal.get_length();
            if (len > 0.0f)
            {
                normal = normal * (1.0f / len);
                d /= len;
            }

            return { normal, d };
        };

        // row3 ± row0/1/2
        m_frustum.left()   = extract_plane(3, 0,  1.0f);  // row3 + row0
        m_frustum.right()  = extract_plane(3, 0, -1.0f);  // row3 - row0
        m_frustum.bottom() = extract_plane(3, 1,  1.0f);  // row3 + row1
        m_frustum.top()    = extract_plane(3, 1, -1.0f);  // row3 - row1
        m_frustum.near()   = extract_plane(3, 2,  1.0f);  // row3 + row2
        m_frustum.far()    = extract_plane(3, 2, -1.0f);  // row3 - row2
    }
} // namespace ID
