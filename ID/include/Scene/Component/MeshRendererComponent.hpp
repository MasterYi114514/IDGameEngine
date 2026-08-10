#pragma once

#include "IDpch.hpp"
#include "Scene/Component/Component.hpp"
#include "Renderer/Mesh/Model.hpp"
#include "Renderer/Mesh/Mesh.hpp"
#include "Renderer/Mesh/MeshFactory.hpp"

namespace ID
{
    class ID_API MeshRendererComponent : public Component
    {
    public:
        MeshRendererComponent() = default;
        explicit MeshRendererComponent(const Model& model);
        explicit MeshRendererComponent(const Model&& model) : m_model(std::move(model)) { }
        virtual ~MeshRendererComponent() override = default;

    public:
        Model&       get_model() { return m_model; }
        const Model& get_model() const { return m_model; }
        void         set_model(const Model& model) { m_model = model; }

        void set_mesh(const MeshID mesh) { m_model.set_mesh(mesh); }
        void set_material(const MaterialInstance& material) { m_model.set_material(material); }

    public:
        Component::TypeID get_type_id() const override
        {
            return get_static_type_id<MeshRendererComponent>();
        }

    public:
        // 序列化与反序列化
        Json serialize(ArenaID arena) const override;
        void deserialize(const Json& json) override;
        std::string get_component_type_name() const override { return "MeshRendererComponent"; }
        
    private:
        Model m_model;
    };
}