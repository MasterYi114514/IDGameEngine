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

        // 从 TransformComponent 同步光源位置/方向
        void sync_from_transform();

    public:
        Component::TypeID get_type_id() const override
        {
            return get_static_type_id<LightComponent>();
        }

    private:
        Light m_light;
    };
} // namespace ID