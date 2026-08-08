#include "Scene/Component/MeshRendererComponent.hpp"
#include "IDJson.hpp"
#include "Log/Log.hpp"

#include "Scene/Component/ComponentFactory.hpp"

namespace ID
{
    MeshRendererComponent::MeshRendererComponent(const Model& model)
        : m_model(model)
    {
    }

    Json MeshRendererComponent::serialize(ArenaID arena) const
    {
        Json json = Json::create_object(arena);
        json.insert("type", Json::create_string(get_component_type_name(), arena));
        json.insert("model", m_model.serialize(arena));
        return json;
    }

    void MeshRendererComponent::deserialize(const Json& json)
    {

        m_model.deserialize(json["model"]);
    }

    ID_REGISTER_COMPONENT(MeshRendererComponent, "MeshRendererComponent");

} // namespace ID
