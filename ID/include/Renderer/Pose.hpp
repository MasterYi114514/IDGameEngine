#pragma once

#include "IDMath.hpp"

namespace ID
{
    // 用于描述一个物体在三维空间中的朝向
    enum class OrientationDescType
    {
        Quaternion,         // 四元数
        FrontUp,            // 前向向量 + 上向量
    };
    using OriType = OrientationDescType;

    // 用于描述一个物体在三维空间中的位置与朝向
    template<OrientationDescType DescType>
    struct Pose;

    // 特化：使用四元数描述朝向
    template<>
    struct ID_API Pose<OrientationDescType::Quaternion>
    {
        Pos3 position = Pos3(0.0f, 0.0f, 0.0f);   // 位置
        Quat orientation = Quat(1.0f, 0.0f, 0.0f, 0.0f); // 朝向（四元数）

        // 获取变换矩阵（4x4），这里的变换为 平移 * 旋转，不包括缩放
        Mat4 get_transform_matrix() const;
    };

    // 特化：使用前向向量 + 上向量描述朝向
    template<>
    struct ID_API Pose<OrientationDescType::FrontUp>
    {
        Pos3 position   = Pos3(0.0f, 0.0f, 0.0f);       // 位置
        Vec3 front      = Vec3(0.0f, 0.0f, -1.0f);      // 前向向量（单位化）
        Vec3 up         = Vec3(0.0f, 1.0f, 0.0f);       // 上向量（单位化）

        // 获取右侧向量
        Vec3 right() const { return Math::cross(front, up); }

        // 获取变换矩阵（4x4），这里的变换为 平移 * 旋转，不包括缩放
        Mat4 get_transform_matrix() const;
    };
};