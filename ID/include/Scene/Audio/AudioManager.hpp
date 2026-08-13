#pragma once

#include "IDpch.hpp"
#include "Scene/Audio/AudioID.hpp"
#include "IDAudio.hpp"

namespace ID
{
    /*
    *   AudioManager 是 ID 引擎层的纯静态音频资源管理器。
    *   职责：从路径加载音频（AssetLibrary::load_audio → AudioEngine::create_clip）、
    *   卸载、AudioID → AudioClipID 映射、路径缓存（AudioID → 加载路径）。
    *   与 ShaderManager/TextureManager 模式一致：serialize 只记录路径字符串，
    *   deserialize 按路径重新加载。
    */
    class ID_API AudioManager
    {
    public:
        AudioManager() = delete;
        ~AudioManager() = delete;

    public:
        // 从路径加载音频，返回 ID 层句柄（内部懒初始化 AudioEngine）
        static AudioID load(const std::string& path);

        // 卸载：释放 AudioEngine clip + 归还槽位 + 清除路径缓存
        static void unload(AudioID audio_id);

        // AudioID → IDAudio 的 AudioClipID（供 AudioSourceComponent::set_clip 使用）
        static AudioClipID get_clip(AudioID audio_id);

        // 查询加载路径（路径缓存命中；未命中返回空串并告警）
        static std::string get_audio_path(AudioID audio_id);

        // 通过底层 clip 句柄反查加载路径（Inspector 展示用；未找到返回空串并告警）
        static std::string get_audio_path_by_clip(AudioClipID clip_id);

        // serialize：只记录加载路径字符串（参照 ShaderManager::serialize_shader）
        static Json serialize_audio(AudioID audio_id, ArenaID arena_id);

        // deserialize：从 Json 读回路径并重新 load
        static AudioID deserialize_audio(const Json& json);
    };
} // namespace ID
