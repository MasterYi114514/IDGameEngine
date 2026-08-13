#pragma once

#include "IDPhysicsCore.hpp"
#include "IDMath.hpp"

namespace ID
{
    /**
     *  碰撞箱，注意这里的碰撞箱只存储局部坐标系下的信息，具体的世界位置和朝向由刚体的 transform 决定。
     *  目前支持的碰撞箱类型有：
     *  1. Box：立方体碰撞体，使用半边长度表示。
     *  2. Sphere：球形碰撞体，使用半径表示。
     *  3. Capsule：胶囊体碰撞体，使用半径和圆柱部分高度表示（不含两个半球）。
     *  4. Plane：无限平面碰撞体，使用法线 normal（需归一化）和平面常数 constant 表示，平面方程为 normal · x = constant。
     *
     *  每当添加一个新的碰撞箱类型时，需要依次完成：
     *     1. 在 ColliderShape::Type 中添加新的类型枚举。
     *     2. 在 ColliderShape::ShapeData 中添加新的类型对应的数据结构
     *     3. 在 ColliderShape 中添加新的静态构造函数，用于创建新的碰撞箱类型。
     */
    struct IDPHYSICS_API ColliderShape
    {
        enum class Type : uint8_t
        {
            Box     = 0,        // 立方体碰撞体
            Sphere  = 1,        // 球形碰撞体
            Capsule = 2,        // 胶囊体碰撞体
            Plane   = 3,        // 无限平面碰撞体
        };

        Type type = Type::Box;

        union ShapeData
        {
            Vec3 half_extents;          // Box：半边长度
            float radius;               // Sphere：半径
            struct
            {
                float radius;           // Capsule：半径
                float height;           // Capsule：圆柱部分高度（不含两个半球）
            } capsule;
            struct
            {
                Vec3  normal;           // Plane：法线（需归一化）
                float constant;         // Plane：平面常数，平面方程为 normal · x = constant
            } plane;

            ShapeData() : half_extents(0.5f, 0.5f, 0.5f) {}
        } m_data;

        static ColliderShape make_box(const Vec3& half_extents);
        static ColliderShape make_sphere(float radius);
        static ColliderShape make_capsule(float radius, float height);
        static ColliderShape make_plane(const Vec3& normal, float constant);   // 平面方程：normal · x = constant，normal 需归一化

        bool operator==(const ColliderShape& other) const;
        bool operator!=(const ColliderShape& other) const { return !(*this == other); }
    };
} // namespace ID
