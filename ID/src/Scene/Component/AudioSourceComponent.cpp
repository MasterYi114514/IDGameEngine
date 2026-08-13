#include "Scene/Component/AudioSourceComponent.hpp"
#include "Scene/Component/TransformComponent.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/Audio/AudioManager.hpp"

#include "Scene/Component/ComponentFactory.hpp"

#include "IDJson.hpp"
#include "IDMath.hpp"
#include "Log/Log.hpp"

namespace ID
{
    ID_REGISTER_COMPONENT(AudioSourceComponent, "AudioSourceComponent");

    // ========== Component 接口 ==========

    void AudioSourceComponent::on_attach(GameObject* owner)
    {
        Component::on_attach(owner);

        // 初始化音源
        m_source_id = AudioEngine::create_source();
        if (!m_source_id.is_valid())
        {
            ID_ERROR("AudioSourceComponent::on_attach：AudioEngine::create_source 失败");
            return;
        }

        // 应用初始属性
        AudioSource& source = AudioEngine::get_source(m_source_id);
        source.set_spatial(m_spatial);

        // 反序列化时记录的待激活状态：音源就绪后恢复激活
        if (m_pending_activate)
        {
            m_pending_activate = false;
            make_active();
        }

        // 挂载时自动播放
        if (m_play_on_attach && m_clip_id.is_valid())
        {
            source.play(m_clip_id);
        }
    }

    void AudioSourceComponent::make_active()
    {
        // 资源检查：音源句柄必须已由 AudioEngine 成功创建
        if (!m_source_id.is_valid())
        {
            ID_WARN("AudioSourceComponent::make_active：音源句柄无效（AudioEngine::create_source 未成功），拒绝激活");
            return;
        }
        Component::make_active();
    }

    void AudioSourceComponent::on_detach()
    {
        if (m_source_id.is_valid())
        {
            AudioEngine::destroy_source(m_source_id);
            m_source_id = AudioSourceID{ AudioSourceID::INVALID };
        }
    }

    void AudioSourceComponent::on_update(Timestep /*ts*/)
    {
        if (!m_source_id.is_valid()) return;
        if (!m_owner) return;

        // 从 TransformComponent 同步位置
        const TransformComponent* transform = m_owner->get_component<TransformComponent>();
        if (transform == nullptr)
        {
            ID_ERROR("AudioSourceComponent::on_update：挂载此组件的 GameObject 必须有 TransformComponent");
            return;
        }

        // TransformComponent 未激活：暂不同步位置
        if (!transform->is_active()) return;

        AudioSource& source = AudioEngine::get_source(m_source_id);
        if (source.is_spatial())
        {
            source.set_position(transform->get_position());
        }
    }

    // ========== 播放控制 ==========

    void AudioSourceComponent::play(AudioClipID clip)
    {
        if (!m_source_id.is_valid())
        {
            ID_WARN("AudioSourceComponent::play：音源尚未初始化");
            return;
        }

        if (clip.is_valid())
        {
            m_clip_id = clip;
        }

        if (m_clip_id.is_valid())
        {
            AudioEngine::get_source(m_source_id).play(m_clip_id);
        }
        else
        {
            ID_WARN("AudioSourceComponent::play：没有设置 AudioClip");
        }
    }

    void AudioSourceComponent::pause()
    {
        if (!m_source_id.is_valid()) return;
        AudioEngine::get_source(m_source_id).pause();
    }

    void AudioSourceComponent::stop()
    {
        if (!m_source_id.is_valid()) return;
        AudioEngine::get_source(m_source_id).stop();
    }

    bool AudioSourceComponent::is_playing() const
    {
        if (!m_source_id.is_valid()) return false;
        return AudioEngine::get_source(m_source_id).is_playing();
    }

    // ========== 属性 ==========

    void AudioSourceComponent::set_volume(float volume)
    {
        if (!m_source_id.is_valid())
        {
            ID_WARN("AudioSourceComponent::set_volume：音源尚未初始化");
            return;
        }
        AudioEngine::get_source(m_source_id).set_volume(volume);
    }

    float AudioSourceComponent::get_volume() const
    {
        if (!m_source_id.is_valid()) return 1.0f;
        return AudioEngine::get_source(m_source_id).get_volume();
    }

    void AudioSourceComponent::set_pitch(float pitch)
    {
        if (!m_source_id.is_valid())
        {
            ID_WARN("AudioSourceComponent::set_pitch：音源尚未初始化");
            return;
        }
        AudioEngine::get_source(m_source_id).set_pitch(pitch);
    }

    float AudioSourceComponent::get_pitch() const
    {
        if (!m_source_id.is_valid()) return 1.0f;
        return AudioEngine::get_source(m_source_id).get_pitch();
    }

    void AudioSourceComponent::set_loop(bool loop)
    {
        if (!m_source_id.is_valid())
        {
            ID_WARN("AudioSourceComponent::set_loop：音源尚未初始化");
            return;
        }
        AudioEngine::get_source(m_source_id).set_loop(loop);
    }

    bool AudioSourceComponent::get_loop() const
    {
        if (!m_source_id.is_valid()) return false;
        return AudioEngine::get_source(m_source_id).get_loop();
    }

    // ========== 空间化 ==========

    void AudioSourceComponent::set_spatial(bool spatial)
    {
        m_spatial = spatial;
        if (!m_source_id.is_valid()) return;
        AudioEngine::get_source(m_source_id).set_spatial(spatial);
    }

    bool AudioSourceComponent::is_spatial() const
    {
        return m_spatial;
    }

    void AudioSourceComponent::set_attenuation(float ref_dist, float max_dist, float rolloff)
    {
        if (!m_source_id.is_valid())
        {
            ID_WARN("AudioSourceComponent::set_attenuation：音源尚未初始化");
            return;
        }
        AudioEngine::get_source(m_source_id).set_attenuation(ref_dist, max_dist, rolloff);
    }

    // ========== 音频片段 ==========

    void AudioSourceComponent::set_clip(AudioClipID clip_id)
    {
        m_clip_id = clip_id;
    }

    // ========== 序列化 ==========

    Json AudioSourceComponent::serialize(ArenaID arena) const
    {
        Json obj = Json::create_object(arena);
        obj.insert("type", Json::create_string(get_component_type_name(), arena));
        obj.insert("is_active", Json(m_is_active));

        obj.insert("spatial", Json(m_spatial));
        obj.insert("play_on_attach", Json(m_play_on_attach));
        obj.insert("volume", Json(static_cast<double>(get_volume())));
        obj.insert("pitch", Json(static_cast<double>(get_pitch())));
        obj.insert("loop", Json(get_loop()));

        // 只记录 clip 的加载路径字符串；未记录时尝试经 AudioManager 反查
        std::string clip_path = m_clip_path;
        if(clip_path.empty())
        {
            clip_path = AudioManager::get_audio_path_by_clip(m_clip_id);
        }
        obj.insert("clip_path", Json::create_string(clip_path, arena));

        return obj;
    }

    void AudioSourceComponent::deserialize(const Json& json)
    {
        if (!json.is_object()) return;

        if (json.contains("spatial"))
            m_spatial = json["spatial"].as_bool();

        if (json.contains("play_on_attach"))
            m_play_on_attach = json["play_on_attach"].as_bool();

        if (json.contains("volume"))
            set_volume(static_cast<float>(json["volume"].as_float()));

        if (json.contains("pitch"))
            set_pitch(static_cast<float>(json["pitch"].as_float()));

        if (json.contains("loop"))
            set_loop(json["loop"].as_bool());

        // 恢复 clip：按路径重新加载（AudioManager 路径缓存命中语义）
        if (json.contains("clip_path"))
        {
            m_clip_path = json["clip_path"].as_cstr();
            if(!m_clip_path.empty())
            {
                AudioID audio_id = AudioManager::load(m_clip_path);
                if(audio_id.is_valid())
                {
                    set_clip(AudioManager::get_clip(audio_id));
                }
                else
                {
                    ID_WARN("AudioSourceComponent::deserialize：音频重新加载失败: {}", m_clip_path);
                    m_clip_path.clear();
                }
            }
        }

        // 恢复激活状态：音源句柄在 on_attach 时才创建，
        // 此处仅记录待激活标志，由 on_attach 在音源就绪后真正激活
        m_pending_activate = false;
        if (json.contains("is_active") && json["is_active"].as_bool())
        {
            m_pending_activate = true;
        }
    }
} // namespace ID
