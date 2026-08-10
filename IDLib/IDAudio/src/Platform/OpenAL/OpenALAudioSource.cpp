/**
 *  @file OpenALAudioSource.cpp
 *  @brief AudioSource 的 OpenAL 实现
 */

#include "Audio/AudioSource.hpp"
#include "Audio/AudioEngine.hpp"

#include <AL/al.h>

namespace ID
{

// ==================== 移动语义 ====================

AudioSource::AudioSource(AudioSource&& other) noexcept
    : m_native(other.m_native)
    , m_spatial(other.m_spatial)
{
    other.m_native = nullptr;
}

AudioSource& AudioSource::operator=(AudioSource&& other) noexcept
{
    if (this != &other)
    {
        destroy();

        m_native  = other.m_native;
        m_spatial = other.m_spatial;

        other.m_native = nullptr;
    }
    return *this;
}

void AudioSource::destroy()
{
    if (m_native)
    {
        ALuint source = static_cast<ALuint>(reinterpret_cast<uintptr_t>(m_native));
        alSourceStop(source);
        alSourcei(source, AL_BUFFER, 0);  // 解除绑定
        alDeleteSources(1, &source);
        m_native = nullptr;
    }
}

// ==================== 辅助 ====================

static ALuint get_al_source(const AudioSource& src)
{
    return static_cast<ALuint>(reinterpret_cast<uintptr_t>(src.get_native_handle()));
}

// ==================== 播放控制 ====================

void AudioSource::play(AudioClipID clip_id)
{
    ALuint al_source = get_al_source(*this);
    if (!al_source || !clip_id.is_valid()) return;

    // 停止当前播放
    alSourceStop(al_source);

    // 绑定音频片段
    AudioClip& clip = AudioEngine::get_clip(clip_id);
    ALuint al_buffer = static_cast<ALuint>(reinterpret_cast<uintptr_t>(clip.get_native_handle()));
    alSourcei(al_source, AL_BUFFER, static_cast<ALint>(al_buffer));

    // 开始播放
    alSourcePlay(al_source);
}

void AudioSource::pause()
{
    ALuint al_source = get_al_source(*this);
    if (al_source) alSourcePause(al_source);
}

void AudioSource::stop()
{
    ALuint al_source = get_al_source(*this);
    if (al_source)
    {
        alSourceStop(al_source);
        alSourcei(al_source, AL_BUFFER, 0);
    }
}

void AudioSource::rewind()
{
    ALuint al_source = get_al_source(*this);
    if (al_source) alSourceRewind(al_source);
}

bool AudioSource::is_playing() const
{
    ALuint al_source = get_al_source(*this);
    if (!al_source) return false;

    ALint state;
    alGetSourcei(al_source, AL_SOURCE_STATE, &state);
    return state == AL_PLAYING;
}

bool AudioSource::is_paused() const
{
    ALuint al_source = get_al_source(*this);
    if (!al_source) return false;

    ALint state;
    alGetSourcei(al_source, AL_SOURCE_STATE, &state);
    return state == AL_PAUSED;
}

bool AudioSource::is_stopped() const
{
    ALuint al_source = get_al_source(*this);
    if (!al_source) return true;

    ALint state;
    alGetSourcei(al_source, AL_SOURCE_STATE, &state);
    return state == AL_STOPPED || state == AL_INITIAL;
}

// ==================== 属性 ====================

void AudioSource::set_loop(bool loop)
{
    ALuint al_source = get_al_source(*this);
    if (al_source) alSourcei(al_source, AL_LOOPING, loop ? AL_TRUE : AL_FALSE);
}

bool AudioSource::get_loop() const
{
    ALuint al_source = get_al_source(*this);
    if (!al_source) return false;

    ALint loop;
    alGetSourcei(al_source, AL_LOOPING, &loop);
    return loop == AL_TRUE;
}

void AudioSource::set_volume(float volume)
{
    ALuint al_source = get_al_source(*this);
    if (al_source) alSourcef(al_source, AL_GAIN, volume);
}

float AudioSource::get_volume() const
{
    ALuint al_source = get_al_source(*this);
    if (!al_source) return 0.0f;

    ALfloat vol;
    alGetSourcef(al_source, AL_GAIN, &vol);
    return vol;
}

void AudioSource::set_pitch(float pitch)
{
    ALuint al_source = get_al_source(*this);
    if (al_source) alSourcef(al_source, AL_PITCH, pitch);
}

float AudioSource::get_pitch() const
{
    ALuint al_source = get_al_source(*this);
    if (!al_source) return 0.0f;

    ALfloat pitch;
    alGetSourcef(al_source, AL_PITCH, &pitch);
    return pitch;
}

// ==================== 空间化 ====================

void AudioSource::set_spatial(bool spatial)
{
    m_spatial = spatial;
    ALuint al_source = get_al_source(*this);
    if (al_source) alSourcei(al_source, AL_SOURCE_RELATIVE, spatial ? AL_FALSE : AL_TRUE);
}

bool AudioSource::is_spatial() const
{
    return m_spatial;
}

void AudioSource::set_position(const Vec3& position)
{
    ALuint al_source = get_al_source(*this);
    if (al_source) alSource3f(al_source, AL_POSITION, position[0], position[1], position[2]);
}

Vec3 AudioSource::get_position() const
{
    ALuint al_source = get_al_source(*this);
    if (!al_source) return Vec3();

    ALfloat x, y, z;
    alGetSource3f(al_source, AL_POSITION, &x, &y, &z);
    return Vec3(x, y, z);
}

void AudioSource::set_velocity(const Vec3& velocity)
{
    ALuint al_source = get_al_source(*this);
    if (al_source) alSource3f(al_source, AL_VELOCITY, velocity[0], velocity[1], velocity[2]);
}

Vec3 AudioSource::get_velocity() const
{
    ALuint al_source = get_al_source(*this);
    if (!al_source) return Vec3();

    ALfloat x, y, z;
    alGetSource3f(al_source, AL_VELOCITY, &x, &y, &z);
    return Vec3(x, y, z);
}

void AudioSource::set_attenuation(float reference_distance,
                                   float max_distance,
                                   float rolloff)
{
    ALuint al_source = get_al_source(*this);
    if (!al_source) return;

    alSourcef(al_source, AL_REFERENCE_DISTANCE, reference_distance);
    alSourcef(al_source, AL_MAX_DISTANCE, max_distance);
    alSourcef(al_source, AL_ROLLOFF_FACTOR, rolloff);
}

} // namespace ID
