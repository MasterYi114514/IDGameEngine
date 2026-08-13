#include "Scene/Audio/AudioManager.hpp"
#include "IDAsset.hpp"
#include "IDAudio.hpp"
#include "Log/Log.hpp"

namespace
{
    // ★ 路径缓存（AudioUINT → 加载路径），serialize 只记录路径、get_audio_path 反查都依赖它
    std::unordered_map<ID::AudioUINT, std::string> g_PathMap;

    // AudioUINT → IDAudio 底层 clip 句柄
    std::unordered_map<ID::AudioUINT, ID::AudioClipID> g_ClipMap;

    // 槽位池：vector 按值存储 + 空闲槽集合（空壳 + 槽位复用，参照 IDRenderer ResourcePool）
    std::vector<ID::AudioClipID>        g_ClipPool;
    std::unordered_set<ID::AudioUINT>   g_FreedSlots;

    // 槽位分配：优先复用空闲槽，否则追加到池尾
    ID::AudioUINT search_slot()
    {
        ID::AudioUINT new_id = static_cast<ID::AudioUINT>(g_ClipPool.size());
        if(!g_FreedSlots.empty())
        {
            auto it = g_FreedSlots.begin();
            new_id = *it;
            g_FreedSlots.erase(it);
        }
        return new_id;
    }
} // 匿名命名空间

namespace ID
{
    AudioID AudioManager::load(const std::string& path)
    {
        // AudioEngine 懒初始化（当前项目无其他 init 调用点，init 幂等）
        if(!AudioEngine::is_initialized())
        {
            if(!AudioEngine::init())
            {
                ID_ERROR("AudioManager::load：AudioEngine 初始化失败，无法加载音频: {}", path);
                return AudioID{};
            }
        }

        AssetPtr<AudioData> asset = AssetLibrary::load_audio(path);
        if(!asset.is_valid())
        {
            ID_ERROR("AudioManager::load：加载音频失败: {}", path);
            return AudioID{};
        }

        AudioClipID clip = AudioEngine::create_clip(asset->data);
        if(!clip.is_valid())
        {
            ID_ERROR("AudioManager::load：创建音频片段失败: {}", path);
            return AudioID{};
        }

        // 槽位分配：优先复用空闲槽，否则追加到池尾
        AudioUINT new_id = search_slot();
        if(new_id >= g_ClipPool.size())
        {
            g_ClipPool.emplace_back(clip);
        }
        else
        {
            g_ClipPool[new_id] = clip;
        }

        g_ClipMap[new_id] = clip;
        g_PathMap[new_id] = path;   // 写入路径缓存

        return AudioID{new_id};
    }

    void AudioManager::unload(AudioID audio_id)
    {
        if(!audio_id.is_valid())
        {
            ID_WARN("AudioManager::unload：无效的 AudioID");
            return;
        }

        auto clip_it = g_ClipMap.find(audio_id.id);
        if(clip_it != g_ClipMap.end())
        {
            AudioEngine::unload_clip(clip_it->second);
            g_ClipMap.erase(clip_it);
        }

        g_PathMap.erase(audio_id.id);

        if(audio_id.id < g_ClipPool.size())
        {
            g_ClipPool[audio_id.id] = AudioClipID{};    // 空壳
        }
        g_FreedSlots.insert(audio_id.id);
    }

    AudioClipID AudioManager::get_clip(AudioID audio_id)
    {
        auto it = g_ClipMap.find(audio_id.id);
        if(it != g_ClipMap.end())
        {
            return it->second;
        }
        ID_WARN("AudioManager::get_clip：未找到 AudioID: {}", audio_id.id);
        return AudioClipID{};
    }

    std::string AudioManager::get_audio_path(AudioID audio_id)
    {
        auto it = g_PathMap.find(audio_id.id);
        if(it != g_PathMap.end())
        {
            return it->second;
        }
        ID_WARN("AudioManager::get_audio_path：未找到 AudioID: {}", audio_id.id);
        return "";
    }

    std::string AudioManager::get_audio_path_by_clip(AudioClipID clip_id)
    {
        for(const auto& [id, clip] : g_ClipMap)
        {
            if(clip == clip_id)
            {
                auto path_it = g_PathMap.find(id);
                if(path_it != g_PathMap.end())
                {
                    return path_it->second;
                }
            }
        }
        ID_WARN("AudioManager::get_audio_path_by_clip：未找到该 clip 句柄");
        return "";
    }

    Json AudioManager::serialize_audio(AudioID audio_id, ArenaID arena_id)
    {
        Json json = Json::create_object(arena_id);
        json.insert("path", Json::create_string(get_audio_path(audio_id), arena_id));
        return json;
    }

    AudioID AudioManager::deserialize_audio(const Json& json)
    {
        std::string path = json["path"].as_cstr();
        return load(path);
    }
} // namespace ID
