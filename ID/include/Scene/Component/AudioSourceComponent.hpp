#pragma once

#include "IDpch.hpp"
#include "Scene/Component/Component.hpp"
#include "IDAudio.hpp"

namespace ID
{
    class ID_API AudioSourceComponent : public Component
    {
    public:
        AudioSourceComponent() = default;
        ~AudioSourceComponent() override = default;

        void on_attach(GameObject* owner) override;
        void on_detach() override;
        void on_update(Timestep ts) override;

        TypeID      get_type_id() const override { return get_static_type_id<AudioSourceComponent>(); }
        std::string get_component_type_name() const override { return "AudioSourceComponent"; }
        bool        allow_multiple() const override { return true; }  // 多个音源叠加

    public:

        void play(AudioClipID clip);
        void pause();
        void stop();
        bool is_playing() const;


        void  set_volume(float volume);
        float get_volume() const;

        void  set_pitch(float pitch);
        float get_pitch() const;

        void set_loop(bool loop);
        bool get_loop() const;


        void set_spatial(bool spatial);   // true=3D / false=2D
        bool is_spatial() const;

        void set_attenuation(float ref_dist, float max_dist, float rolloff = 1.0f);

        void set_clip(AudioClipID clip_id);
        AudioClipID get_clip() const { return m_clip_id; }

    public:
        Json serialize(ArenaID arena) const override;
        void deserialize(const Json& json) override;

    private:
        AudioSourceID m_source_id;      // 在 AudioEngine 池中的句柄
        AudioClipID   m_clip_id;        // 当前关联的音频片段
        bool          m_play_on_attach = false;  // 挂载时自动播放
        bool          m_spatial = true;
    };
}