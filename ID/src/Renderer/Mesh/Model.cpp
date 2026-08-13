#include "Renderer/Mesh/MeshFactory.hpp"
#include "Renderer/Mesh/Model.hpp"
#include "Renderer/Material/MaterialInstance.hpp"
#include "Renderer/Material/MaterialLibrary.hpp"

namespace ID
{
    Json Model::serialize(ArenaID arena_id) const
    {
        Json json = Json::create_object(arena_id);
        json.insert("mesh", MeshFactory::serialize(m_mesh, arena_id));
        json.insert("material", m_material.serialize(arena_id));
        json.insert("local_transform", JSON::create(m_local_transform, arena_id));
        return json;
    }

    void Model::deserialize(const Json& json)
    {
        m_mesh = MeshFactory::deserialize(json["mesh"]);
        m_material.deserialize(json["material"]);
        m_local_transform = JSON::parse<Mat4>(json["local_transform"]);
    }
}