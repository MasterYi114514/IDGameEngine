#include "Renderer/Pose.hpp"

namespace ID
{
    Mat4 Pose<OrientationDescType::Quaternion>::get_transform_matrix() const
    {
        // T * R：平移 * 旋转，不包含缩放
        return Math::get_translation(position) * orientation.to_mat4();
    }

    Mat4 Pose<OrientationDescType::FrontUp>::get_transform_matrix() const
    {
        // 构建正交基：right = front × up, corrected_up = right × front
        Vec3 r = Math::cross(front, up);
        r.normalize();
        Vec3 u = Math::cross(r, front);

        Mat4 rot = Math::get_identity_mat4();
        // 列主序旋转矩阵：列0=right, 列1=up, 列2=-front
        rot.element(0, 0) = r[0]; rot.element(0, 1) = u[0]; rot.element(0, 2) = -front[0];
        rot.element(1, 0) = r[1]; rot.element(1, 1) = u[1]; rot.element(1, 2) = -front[1];
        rot.element(2, 0) = r[2]; rot.element(2, 1) = u[2]; rot.element(2, 2) = -front[2];

        // T * R
        return Math::get_translation(position) * rot;
    }
}