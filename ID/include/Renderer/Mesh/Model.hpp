#pragma once

#include "IDpch.hpp"
#include "Renderer/Mesh/MeshFactory.hpp"
#include "Renderer/Material/MaterialInstance.hpp"

namespace ID
{
    class ID_API Model : public SerializableBase
    {
    public:
        Model() = default;
        Model(MeshID mesh, const MaterialInstance& material, 
            const Mat4& local_transform = Math::get_identity_mat4())
            : m_mesh(mesh), m_material(material), m_local_transform(local_transform) { }

        Model(const Model&) = default;
        Model& operator=(const Model&) = default;
        Model(Model&&) noexcept = default;
        Model& operator=(Model&&) noexcept = default;

    public:
        MeshID              get_mesh_id()              const { return m_mesh; }
        Mesh&               get_mesh()                 const { return MeshFactory::get_mesh(m_mesh); }
        void                set_mesh(MeshID mesh)            { m_mesh = mesh; }

        MaterialInstance&       get_material()       { return m_material; }
        const MaterialInstance& get_material() const { return m_material; }
        void                    set_material(const MaterialInstance& material) { m_material = material; }

        const Mat4&         get_local_transform() const { return m_local_transform; }
        void                set_local_transform(const Mat4& transform) { m_local_transform = transform; }

        bool is_valid() const { return m_mesh.is_valid() && m_material.is_valid(); }

    public:
        Json serialize(ArenaID arena_id) const override;
        void deserialize(const Json& json) override;

    private:
        MeshID              m_mesh = MeshID::invalid_id();
        MaterialInstance    m_material = MaterialInstance(nullptr);
        Mat4                m_local_transform = Math::get_identity_mat4();
    };
} // namespace ID