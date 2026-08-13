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

        // 音源句柄（AudioEngine 池）有效才能激活，否则保持 inactive
        void make_active() override;

        TypeID      get_type_id() const override { return get_static_type_id<AudioSourceComponent>(); }
        std::string get_component_type_name() const override { return "AudioSourceComponent"; }
        static constexpr bool s_allow_multiple = true;  // 多个音源叠加

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

        // 记录 clip 的加载路径（供场景序列化恢复用）
        void set_clip_path(const std::string& path) { m_clip_path = path; }
        const std::string& get_clip_path() const { return m_clip_path; }

    public:
        Json serialize(ArenaID arena) const override;
        void deserialize(const Json& json) override;

    private:
        AudioSourceID m_source_id;      // 在 AudioEngine 池中的句柄
        AudioClipID   m_clip_id;        // 当前关联的音频片段
        std::string   m_clip_path;      // clip 的加载路径（序列化只记录路径，反序列化按路径重载）
        bool          m_play_on_attach = false;  // 挂载时自动播放
        bool          m_spatial = true;
        bool          m_pending_activate = false;  // 反序列化时待恢复的激活状态
    };
}