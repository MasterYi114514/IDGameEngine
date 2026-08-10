#pragma once

#include "IDPhysicsCore.hpp"
#include "IDMath.hpp"

namespace ID
{
    struct IDPHYSICS_API ColliderShape
    {
        enum class Type : uint8_t
        {
            Box     = 0,
            Sphere  = 1,
            Capsule = 2,
        };

        Type type = Type::Box;

        // Box
        Vec3 half_extents = Vec3(0.5f, 0.5f, 0.5f);

        // Sphere
        float radius = 0.5f;

        // Capsule
        float capsule_radius = 0.5f;
        float capsule_height = 1.0f;

        static ColliderShape make_box(const Vec3& half_extents);
        static ColliderShape make_sphere(float radius);
        static ColliderShape make_capsule(float radius, float height);

        bool operator==(const ColliderShape& other) const;
        bool operator!=(const ColliderShape& other) const { return !(*this == other); }
    };
} // namespace ID
