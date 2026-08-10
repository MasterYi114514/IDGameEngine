#pragma once

#include "IDPhysicsCore.hpp"

namespace ID
{
    struct IDPHYSICS_API PhysicsMaterial
    {
        float friction         = 0.5f;
        float restitution      = 0.0f;
        float rolling_friction = 0.0f;

        bool operator==(const PhysicsMaterial& other) const
        {
            return friction == other.friction
                && restitution == other.restitution
                && rolling_friction == other.rolling_friction;
        }
        bool operator!=(const PhysicsMaterial& other) const { return !(*this == other); }
    };
} // namespace ID
