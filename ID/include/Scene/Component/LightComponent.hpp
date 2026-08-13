#pragma once

#include "IDpch.hpp"
#include "Scene/Component/Component.hpp"
#include "Renderer/Light/Light.hpp"

namespace ID
{
    class ID_API LightComponent : public Component
    {
    public:
        explicit LightComponent(const Light& light = Light()) : m_light(light) { }
        virtual ~LightComponent() override = default;

    public:
        Light&          get_light() { return m_light; }
        const Light&    get_light() const { return m_light; }
        void            set_light(const Light& light) { m_light = light; }

        void set_enabled(bool enabled) { m_light.enabled = enabled; }
        bool is_enabled()        const { return m_light.enabled; }

    public:
        Component::TypeID get_type_id() const override
        {
            return get_static_type_id<LightComponent>();
        }
    
    public:
        // 序列化与反序列化
        Json        serialize(ArenaID arena_id) const override;
        void        deserialize(const Json& json) override;
        std::string get_component_type_name() const override { return "LightComponent"; }

    private:
        Light m_light;
    };
} // namespace ID