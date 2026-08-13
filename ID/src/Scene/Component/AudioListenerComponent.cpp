#include "Scene/Component/AudioListenerComponent.hpp"
#include "Scene/Component/TransformComponent.hpp"
#include "Scene/GameObject.hpp"

#include "Scene/Component/ComponentFactory.hpp"

#include "IDJson.hpp"
#include "IDMath.hpp"
#include "Log/Log.hpp"

namespace ID
{
    ID_REGISTER_COMPONENT(AudioListenerComponent, "AudioListenerComponent");

    // ========== Component 接口 ==========

    void AudioListenerComponent::on_attach(GameObject* owner)
    {
        Component::on_attach(owner);
    }

    void AudioListenerComponent::on_detach()
    {
        // 监听器本身不持有资源，无需清理
    }

    void AudioListenerComponent::on_update(Timestep /*ts*/)
    {
        if (!m_owner) return;

        const TransformComponent* transform = m_owner->get_component<TransformComponent>();
        if (transform == nullptr)
        {
            ID_ERROR("AudioListenerComponent::on_update：挂载此组件的 GameObject 必须有 TransformComponent");
            return;
        }

        // TransformComponent 未激活：暂不同步监听器
        if (!transform->is_active()) return;

        // 从 TransformComponent 读取位置和朝向
        const Pos3& pos = transform->get_position();
        const Quat& rot = transform->get_orientation();

        // 前方向：局部 -Z 经旋转
        Vec3 forward = rot * Vec3(0.0f, 0.0f, -1.0f);
        forward.normalize();

        // 上方向：局部 +Y 经旋转
        Vec3 up = rot * Vec3(0.0f, 1.0f, 0.0f);
        up.normalize();

        AudioListener listener;
        listener.position = pos;
        listener.forward  = forward;
        listener.up       = up;

        AudioEngine::set_listener(listener);
    }

    // ========== 序列化 ==========

    Json AudioListenerComponent::serialize(ArenaID arena) const
    {
        Json obj = Json::create_object(arena);
        obj.insert("type", Json::create_string(get_component_type_name(), arena));
        obj.insert("is_active", Json(m_is_active));
        return obj;
    }

    void AudioListenerComponent::deserialize(const Json& json)
    {
        // 监听器组件无额外数据；仅恢复激活状态（无资源依赖，直接激活）
        if (json.contains("is_active") && json["is_active"].as_bool())
        {
            make_active();
        }
    }
} // namespace ID
