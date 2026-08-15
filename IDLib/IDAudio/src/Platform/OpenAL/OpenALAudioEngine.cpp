/**
 *  @file OpenALAudioEngine.cpp
 *  @brief AudioEngine 的 OpenAL 实现
 *
 *  本文件是 OpenAL 头文件唯一出现的地方。
 *  通过匿名命名空间封装内部状态。
 */

#include "Audio/AudioEngine.hpp"
#include "Loader/AudioLoader.hpp"
#include "Log/Log.hpp"

// OpenAL 头文件（仅此文件可包含）
#include <AL/al.h>
#include <AL/alc.h>

#include <vector>
#include <unordered_set>

namespace ID
{

// ==================== 匿名命名空间：内部状态 ====================

namespace
{
    ALCdevice*  g_device    = nullptr;
    ALCcontext* g_context   = nullptr;
    bool        g_initialized = false;
    float       g_master_volume = 1.0f;

    // 音源池
    std::vector<AudioSource>                        g_source_pool;
    std::unordered_set<AudioSourceID::UnderlyingType> g_freed_source_ids;

    // 片段池
    std::vector<AudioClip>                          g_clip_pool;
    std::unordered_set<AudioClipID::UnderlyingType>   g_freed_clip_ids;

    // ========== 辅助函数 ==========

    /**
     *  @brief 将 ALenum 错误码转为可读字符串
     */
    const char* al_error_string(ALenum error)
    {
        switch (error)
        {
            case AL_NO_ERROR:          return "AL_NO_ERROR";
            case AL_INVALID_NAME:      return "AL_INVALID_NAME";
            case AL_INVALID_ENUM:      return "AL_INVALID_ENUM";
            case AL_INVALID_VALUE:     return "AL_INVALID_VALUE";
            case AL_INVALID_OPERATION: return "AL_INVALID_OPERATION";
            case AL_OUT_OF_MEMORY:     return "AL_OUT_OF_MEMORY";
            default:                   return "UNKNOWN_AL_ERROR";
        }
    }

    void check_al_error(const char* operation)
    {
        ALenum error = alGetError();
        if (error != AL_NO_ERROR)
        {
            IDAUDIO_ERROR("OpenAL error during '{}': {}", operation, al_error_string(error));
        }
    }

    /**
     *  @brief 槽位分配（与 IDRenderer ResourceManager 一致）
     *  @tparam PoolType 池元素类型
     *  @tparam IDType   ID 下标类型
     */
    template<typename PoolType, typename IDType>
    IDType search_slot(const std::vector<PoolType>& pool, std::unordered_set<IDType>& freed_ids)
    {
        if (!freed_ids.empty())
        {
            auto it = freed_ids.begin();
            IDType id = *it;
            freed_ids.erase(it);
            return id;
        }
        return static_cast<IDType>(pool.size());
    }

} // anonymous namespace

// ==================== AudioEngine 实现 ====================

bool AudioEngine::init()
{
    if (g_initialized) return true;

    // 打开默认设备
    g_device = alcOpenDevice(nullptr);
    if (!g_device)
    {
        IDAUDIO_ERROR("Failed to open OpenAL device");
        return false;
    }

    // 创建上下文
    g_context = alcCreateContext(g_device, nullptr);
    if (!g_context)
    {
        IDAUDIO_ERROR("Failed to create OpenAL context");
        alcCloseDevice(g_device);
        g_device = nullptr;
        return false;
    }

    alcMakeContextCurrent(g_context);

    // 设置默认监听器
    alListener3f(AL_POSITION, 0.0f, 0.0f, 0.0f);
    alListener3f(AL_VELOCITY, 0.0f, 0.0f, 0.0f);
    ALfloat listener_orientation[] = { 0.0f, 0.0f, -1.0f,  0.0f, 1.0f, 0.0f };
    alListenerfv(AL_ORIENTATION, listener_orientation);

    g_initialized = true;
    IDAUDIO_INFO("AudioEngine initialized successfully");
    return true;
}

void AudioEngine::shutdown()
{
    if (!g_initialized) return;

    // 销毁所有音源
    for (auto& source : g_source_pool)
    {
        source.destroy();
    }
    g_source_pool.clear();
    g_freed_source_ids.clear();

    // 销毁所有片段
    for (auto& clip : g_clip_pool)
    {
        clip.destroy();
    }
    g_clip_pool.clear();
    g_freed_clip_ids.clear();

    // 销毁上下文和设备
    alcMakeContextCurrent(nullptr);
    if (g_context)
    {
        alcDestroyContext(g_context);
        g_context = nullptr;
    }
    if (g_device)
    {
        alcCloseDevice(g_device);
        g_device = nullptr;
    }

    g_initialized = false;
    IDAUDIO_INFO("音频引擎已关闭：全部音源与音频片段（OpenAL 缓冲区）已销毁，上下文与默认设备已释放");
}

bool AudioEngine::is_initialized()
{
    return g_initialized;
}

// ========== 监听器 ==========

void AudioEngine::set_listener_position(const Vec3& position)
{
    if (!g_initialized) return;
    alListener3f(AL_POSITION, position[0], position[1], position[2]);
}

void AudioEngine::set_listener_orientation(const Vec3& forward, const Vec3& up)
{
    if (!g_initialized) return;
    ALfloat orientation[] = {
        forward[0], forward[1], forward[2],
        up[0], up[1], up[2]
    };
    alListenerfv(AL_ORIENTATION, orientation);
}

void AudioEngine::set_listener_velocity(const Vec3& velocity)
{
    if (!g_initialized) return;
    alListener3f(AL_VELOCITY, velocity[0], velocity[1], velocity[2]);
}

void AudioEngine::set_listener(const AudioListener& listener)
{
    set_listener_position(listener.position);
    set_listener_orientation(listener.forward, listener.up);
    set_listener_velocity(listener.velocity);
}

// ========== 全局控制 ==========

void AudioEngine::set_master_volume(float volume)
{
    g_master_volume = volume;
    alListenerf(AL_GAIN, volume);
}

float AudioEngine::get_master_volume()
{
    return g_master_volume;
}

// ========== 音源管理 ==========

AudioSourceID AudioEngine::create_source()
{
    if (!g_initialized) return AudioSourceID{ AudioSourceID::INVALID };

    ALuint al_source = 0;
    alGenSources(1, &al_source);
    if (al_source == 0)
    {
        IDAUDIO_ERROR("Failed to generate OpenAL source");
        return AudioSourceID{ AudioSourceID::INVALID };
    }

    // 默认属性
    alSourcef(al_source, AL_GAIN, 1.0f);
    alSourcef(al_source, AL_PITCH, 1.0f);
    alSource3f(al_source, AL_POSITION, 0.0f, 0.0f, 0.0f);
    alSource3f(al_source, AL_VELOCITY, 0.0f, 0.0f, 0.0f);
    alSourcei(al_source, AL_LOOPING, AL_FALSE);
    alSourcei(al_source, AL_SOURCE_RELATIVE, AL_FALSE);  // 3D 模式

    AudioSourceID::UnderlyingType slot = search_slot(g_source_pool, g_freed_source_ids);

    // 存储 native handle（reinterpret_cast 因为 void* 不能直接存 ALuint）
    AudioSource source;
    source.m_native = reinterpret_cast<void*>(static_cast<uintptr_t>(al_source));

    if (slot >= g_source_pool.size())
    {
        g_source_pool.push_back(std::move(source));
    }
    else
    {
        g_source_pool[slot] = std::move(source);
    }

    IDAUDIO_DEBUG("AudioSource created: slot={}", slot);
    return AudioSourceID{ slot };
}

void AudioEngine::destroy_source(AudioSourceID id)
{
    if (!g_initialized || !id.is_valid()) return;
    if (id.id >= g_source_pool.size()) return;
    if (g_freed_source_ids.find(id.id) != g_freed_source_ids.end()) return;

    g_source_pool[id.id].destroy();
    g_freed_source_ids.insert(id.id);

    IDAUDIO_DEBUG("AudioSource destroyed: slot={}", id.id);
}

AudioSource& AudioEngine::get_source(AudioSourceID id)
{
    return g_source_pool.at(id.id);
}

// ========== 音频片段管理 ==========

AudioClipID AudioEngine::create_clip(const AudioData& data)
{
    if (!g_initialized) return AudioClipID{ AudioClipID::INVALID };

    // 槽位分配
    AudioClipID::UnderlyingType slot = search_slot(g_clip_pool, g_freed_clip_ids);

    // 创建 AudioClip 并提交 PCM 数据到 OpenAL
    AudioClip clip;
    if (!clip.create_from_audio_data(data))
    {
        IDAUDIO_ERROR("AudioEngine::create_clip：create_from_audio_data 失败");
        return AudioClipID{ AudioClipID::INVALID };
    }

    // 先保存属性（move 后 clip 变空壳）
    uint32_t sample_rate = clip.get_sample_rate();
    uint8_t  channels    = clip.get_channels();
    float    duration    = clip.get_duration();

    if (slot >= g_clip_pool.size())
    {
        g_clip_pool.push_back(std::move(clip));
    }
    else
    {
        g_clip_pool[slot] = std::move(clip);
    }

    IDAUDIO_INFO("AudioClip created → slot={} ({}Hz, {}ch, {:.2f}s)",
                 slot, sample_rate, channels, duration);
    return AudioClipID{ slot };
}

void AudioEngine::unload_clip(AudioClipID id)
{
    if (!g_initialized || !id.is_valid()) return;
    if (id.id >= g_clip_pool.size()) return;
    if (g_freed_clip_ids.find(id.id) != g_freed_clip_ids.end()) return;

    g_clip_pool[id.id].destroy();
    g_freed_clip_ids.insert(id.id);

    IDAUDIO_DEBUG("AudioClip unloaded: slot={}", id.id);
}

AudioClip& AudioEngine::get_clip(AudioClipID id)
{
    return g_clip_pool.at(id.id);
}

// ========== 每帧更新 ==========

void AudioEngine::on_update()
{
    // Phase 6 暂无可流式音频，留空
}

} // namespace ID
