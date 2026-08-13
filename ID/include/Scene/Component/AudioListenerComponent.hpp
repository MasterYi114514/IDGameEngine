#pragma once

#include "IDpch.hpp"
#include "Scene/Component/Component.hpp"
#include "IDAudio.hpp"

namespace ID
{
    /**
     *  @brief 音频监听器组件
     *
     *  通常挂载到摄像机 GameObject。
     *  每帧从 TransformComponent 同步位置/朝向到 AudioEngine
     */
    class ID_API AudioListenerComponent : public Component
    {
    public:
        AudioListenerComponent() = default;
        ~AudioListenerComponent() override = default;

        // ========== Component 接口 ==========

        void on_attach(GameObject* owner) override;
        void on_detach() override;
        void on_update(Timestep ts) override;

        TypeID      get_type_id() const override { return get_static_type_id<AudioListenerComponent>(); }
        std::string get_component_type_name() const override { return "AudioListenerComponent"; }
        bool        allow_multiple() const override { return false; }  // 全局唯一监听器

        // ========== 序列化 ==========

        Json serialize(ArenaID arena) const override;
        void deserialize(const Json& json) override;
    };
}