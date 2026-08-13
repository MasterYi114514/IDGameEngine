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
        void         set_model(const Model& model);

        void set_mesh(const MeshID mesh);
        void set_material(const MaterialInstance& material);

    public:
        // 资源（Mesh + Material）有效才能激活，否则保持 inactive
        void make_active() override;

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