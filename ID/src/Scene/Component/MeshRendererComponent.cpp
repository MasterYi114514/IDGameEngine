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

    void MeshRendererComponent::make_active()
    {
        // 资源检查：Mesh 和 Material 都有效才能激活
        if (!m_model.is_valid())
        {
            ID_WARN("MeshRendererComponent::make_active：Model 无效（Mesh 或 Material 缺失），拒绝激活");
            return;
        }
        Component::make_active();
    }

    void MeshRendererComponent::set_model(const Model& model)
    {
        m_model = model;
        if (!m_model.is_valid())
        {
            make_inactive();
        }
    }

    void MeshRendererComponent::set_mesh(const MeshID mesh)
    {
        m_model.set_mesh(mesh);
        if (!m_model.is_valid())
        {
            make_inactive();
        }
    }

    void MeshRendererComponent::set_material(const MaterialInstance& material)
    {
        m_model.set_material(material);
        if (!m_model.is_valid())
        {
            make_inactive();
        }
    }

    Json MeshRendererComponent::serialize(ArenaID arena) const
    {
        Json json = Json::create_object(arena);
        json.insert("type", Json::create_string(get_component_type_name(), arena));
        json.insert("is_active", Json(m_is_active));
        json.insert("model", m_model.serialize(arena));
        return json;
    }

    void MeshRendererComponent::deserialize(const Json& json)
    {

        m_model.deserialize(json["model"]);

        // 恢复激活状态（make_active 会再次校验资源有效性）
        if (json.contains("is_active") && json["is_active"].as_bool())
        {
            make_active();
        }
    }

    ID_REGISTER_COMPONENT(MeshRendererComponent, "MeshRendererComponent");

} // namespace ID
