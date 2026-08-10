/**
 *  @file OpenALAudioClip.cpp
 *  @brief AudioClip 的 OpenAL 实现
 */

#include "Audio/AudioClip.hpp"
#include "Loader/AudioLoader.hpp"
#include "Log/Log.hpp"

#include <AL/al.h>

namespace ID
{

// ==================== 移动语义 ====================

AudioClip::AudioClip(AudioClip&& other) noexcept
    : m_native(other.m_native)
    , m_duration(other.m_duration)
    , m_sample_rate(other.m_sample_rate)
    , m_channels(other.m_channels)
{
    other.m_native = nullptr;
    other.m_duration = 0.0f;
    other.m_sample_rate = 0;
    other.m_channels = 0;
}

AudioClip& AudioClip::operator=(AudioClip&& other) noexcept
{
    if (this != &other)
    {
        destroy();

        m_native      = other.m_native;
        m_duration    = other.m_duration;
        m_sample_rate = other.m_sample_rate;
        m_channels    = other.m_channels;

        other.m_native      = nullptr;
        other.m_duration    = 0.0f;
        other.m_sample_rate = 0;
        other.m_channels    = 0;
    }
    return *this;
}

void AudioClip::destroy()
{
    if (m_native)
    {
        ALuint buffer = static_cast<ALuint>(reinterpret_cast<uintptr_t>(m_native));
        alDeleteBuffers(1, &buffer);
        m_native = nullptr;
    }
    m_duration = 0.0f;
    m_sample_rate = 0;
    m_channels = 0;
}

bool AudioClip::create_from_audio_data(const AudioData& data)
{
    // 校验数据合法性（不合法直接返回，不产生 OpenAL 资源）
    if(data.pcm_data.empty() || data.sample_rate == 0)
    {
        IDAUDIO_ERROR("AudioClip::create_from_audio_data：无效的 AudioData（空数据或采样率为 0）");
        return false;
    }

    // 根据 channels / bits_per_sample 确定 OpenAL 格式
    ALenum format;
    if(data.channels == 1)
    {
        format = (data.bits_per_sample == 16) ? AL_FORMAT_MONO16 : AL_FORMAT_MONO8;
    }
    else if(data.channels == 2)
    {
        format = (data.bits_per_sample == 16) ? AL_FORMAT_STEREO16 : AL_FORMAT_STEREO8;
    }
    else
    {
        IDAUDIO_ERROR("AudioClip::create_from_audio_data：不支持的声道数: {}", data.channels);
        return false;
    }

    // 释放旧 buffer（重复调用无泄漏）
    destroy();

    ALuint al_buffer = 0;
    alGenBuffers(1, &al_buffer);
    if(al_buffer == 0)
    {
        IDAUDIO_ERROR("AudioClip::create_from_audio_data：alGenBuffers 失败");
        return false;
    }

    alBufferData(al_buffer, format,
                 data.pcm_data.data(),
                 static_cast<ALsizei>(data.pcm_data.size()),
                 static_cast<ALsizei>(data.sample_rate));
    if(alGetError() != AL_NO_ERROR)
    {
        IDAUDIO_ERROR("AudioClip::create_from_audio_data：alBufferData 失败");
        alDeleteBuffers(1, &al_buffer);
        return false;
    }

    m_native      = reinterpret_cast<void*>(static_cast<uintptr_t>(al_buffer));
    m_sample_rate = data.sample_rate;
    m_channels    = data.channels;
    m_duration    = static_cast<float>(data.pcm_data.size())
                  / static_cast<float>(data.sample_rate * data.channels * (data.bits_per_sample / 8));

    return true;
}

} // namespace ID
