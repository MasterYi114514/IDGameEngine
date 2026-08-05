#include "Scene/Component/LightComponent.hpp"
#include "Scene/Component/TransformComponent.hpp"
#include "Scene/GameObject.hpp"

namespace ID
{
    void LightComponent::sync_from_transform()
    {
        if (m_owner == nullptr)
        {
            return;
        }

        const TransformComponent& transform = m_owner->get_transform();
        const Pos3& position = transform.get_position();

        switch (m_light.type)
        {
            case LightType::Directional:
            {
                // 平行光方向：取变换的 -Z 前方向（未经缩放）
                const Quat& q = transform.get_orientation();
                Vec3 front = q * Vec3(0.0f, 0.0f, -1.0f);
                front.normalize();
                m_light.drop.direction = front;
                break;
            }
            case LightType::Point:
            case LightType::Spot:
            {
                m_light.drop.position = position;
                break;
            }
            default:
                break;
        }
    }
}