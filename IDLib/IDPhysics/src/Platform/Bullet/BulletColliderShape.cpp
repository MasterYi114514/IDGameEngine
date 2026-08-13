#include "Physics/ColliderShape.hpp"

namespace ID
{

ColliderShape ColliderShape::make_box(const Vec3& half_extents)
{
    ColliderShape shape;
    shape.type = Type::Box;
    shape.m_data.half_extents = half_extents;
    return shape;
}

ColliderShape ColliderShape::make_sphere(float radius)
{
    ColliderShape shape;
    shape.type = Type::Sphere;
    shape.m_data.radius = radius;
    return shape;
}

ColliderShape ColliderShape::make_capsule(float radius, float height)
{
    ColliderShape shape;
    shape.type = Type::Capsule;
    shape.m_data.capsule.radius = radius;
    shape.m_data.capsule.height = height;
    return shape;
}

ColliderShape ColliderShape::make_plane(const Vec3& normal, float constant)
{
    ColliderShape shape;
    shape.type = Type::Plane;
    shape.m_data.plane.normal = normal;
    shape.m_data.plane.constant = constant;
    return shape;
}

bool ColliderShape::operator==(const ColliderShape& other) const
{
    if (type != other.type) return false;
    switch (type)
    {
        case Type::Box:     return m_data.half_extents == other.m_data.half_extents;
        case Type::Sphere:  return m_data.radius == other.m_data.radius;
        case Type::Capsule: return m_data.capsule.radius == other.m_data.capsule.radius
                                  && m_data.capsule.height == other.m_data.capsule.height;
        case Type::Plane:   return m_data.plane.normal == other.m_data.plane.normal
                                  && m_data.plane.constant == other.m_data.plane.constant;
        default: return false;
    }
}

} // namespace ID
