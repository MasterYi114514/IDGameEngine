#include "Scene/Component/LightComponent.hpp"
#include "Scene/Component/TransformComponent.hpp"
#include "Scene/GameObject.hpp"

#include "Scene/Component/ComponentFactory.hpp"

#include "IDJson.hpp"
#include "Log/Log.hpp"

namespace ID
{
    Json LightComponent::serialize(ArenaID arena_id) const
    {
        Json obj = Json::create_object(arena_id);
        obj.insert("type", Json::create_string(get_component_type_name(), arena_id));
        obj.insert("is_active", Json(m_is_active));
        obj.insert("m_light", m_light.serialize(arena_id));

        return obj;
    }

    void LightComponent::deserialize(const Json& json)
    {
        m_light.deserialize(json["m_light"]);

        // 恢复激活状态（Light 无外部资源依赖，直接激活）
        if (json.contains("is_active") && json["is_active"].as_bool())
        {
            make_active();
        }
    }

    ID_REGISTER_COMPONENT(LightComponent, "LightComponent");
}