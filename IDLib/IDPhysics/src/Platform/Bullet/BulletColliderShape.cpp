#include "Physics/ColliderShape.hpp"

namespace ID
{

ColliderShape ColliderShape::make_box(const Vec3& half_extents)
{
    ColliderShape shape;
    shape.type = Type::Box;
    shape.half_extents = half_extents;
    return shape;
}

ColliderShape ColliderShape::make_sphere(float radius)
{
    ColliderShape shape;
    shape.type = Type::Sphere;
    shape.radius = radius;
    return shape;
}

ColliderShape ColliderShape::make_capsule(float radius, float height)
{
    ColliderShape shape;
    shape.type = Type::Capsule;
    shape.capsule_radius = radius;
    shape.capsule_height = height;
    return shape;
}

bool ColliderShape::operator==(const ColliderShape& other) const
{
    if (type != other.type) return false;
    switch (type)
    {
        case Type::Box:     return half_extents == other.half_extents;
        case Type::Sphere:  return radius == other.radius;
        case Type::Capsule: return capsule_radius == other.capsule_radius
                                  && capsule_height == other.capsule_height;
        default: return false;
    }
}

} // namespace ID
