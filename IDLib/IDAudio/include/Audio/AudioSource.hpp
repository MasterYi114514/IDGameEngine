#pragma once

#include "IDAudioCore.hpp"
#include "IDMath.hpp"
#include "Audio/AudioClip.hpp"

namespace ID
{
    /**
     *  @brief 音源句柄（槽位下标）
     */
    struct IDAUDIO_API AudioSourceID
    {
        using UnderlyingType = uint32_t;
        static constexpr UnderlyingType INVALID = static_cast<UnderlyingType>(-1);

        UnderlyingType id = INVALID;

        bool is_valid() const { return id != INVALID; }
        bool operator==(const AudioSourceID& other) const { return id == other.id; }
        bool operator!=(const AudioSourceID& other) const { return id != other.id; }

        struct Hash
        {
            size_t operator()(const AudioSourceID& sid) const
            {
                return std::hash<UnderlyingType>()(sid.id);
            }
        };
    };

    /**
     *  @brief 3D/2D 音频源
     *
     *  封装 OpenAL Source。支持：
     *  - 播放 / 暂停 / 停止 / 循环
     *  - 音量 / 音高控制
     *  - 3D 空间化（位置衰减）+ 2D 模式切换
     *
     *  遵循"空壳模式"。
     */
    class IDAUDIO_API AudioSource
    {
    public:
        AudioSource() = default;
        ~AudioSource() { destroy(); }

        AudioSource(const AudioSource&) = delete;
        AudioSource& operator=(const AudioSource&) = delete;
        AudioSource(AudioSource&& other) noexcept;
        AudioSource& operator=(AudioSource&& other) noexcept;

        void destroy();

        // ========== 播放控制 ==========

        /**
         *  @brief 播放指定的音频片段
         *  @param clip_id 音频片段句柄
         *
         *  如果正在播放则先停止。
         */
        void play(AudioClipID clip_id);

        void pause();
        void stop();
        void rewind();

        bool is_playing() const;
        bool is_paused()  const;
        bool is_stopped() const;

        // ========== 属性 ==========

        void set_loop(bool loop);
        bool get_loop() const;

        /**
         *  @brief 设置音量
         *  @param volume 0.0 = 静音，1.0 = 最大
         */
        void  set_volume(float volume);
        float get_volume() const;

        /**
         *  @brief 设置音高（播放速度）
         *  @param pitch 默认 1.0，范围通常 0.5~2.0
         */
        void  set_pitch(float pitch);
        float get_pitch() const;

        // ========== 空间化 ==========

        /**
         *  @brief 设置是否启用 3D 空间化
         *  @param spatial true=3D（位置衰减），false=2D（全局音量）
         */
        void set_spatial(bool spatial);
        bool is_spatial() const;

        /**
         *  @brief 设置 3D 音源位置（世界空间）
         */
        void set_position(const Vec3& position);
        Vec3 get_position() const;

        /**
         *  @brief 设置音源速度（用于多普勒效应）
         */
        void set_velocity(const Vec3& velocity);
        Vec3 get_velocity() const;

        /**
         *  @brief 设置衰减参数
         *  @param reference_distance 参考距离（小于此距离 = 最大音量）
         *  @param max_distance      最大距离（大于此距离 = 静音）
         *  @param rolloff           衰减系数（默认 1.0 = 线性衰减）
         */
        void set_attenuation(float reference_distance,
                             float max_distance,
                             float rolloff = 1.0f);

        // ========== 内部 ==========

        void* get_native_handle() const { return m_native; }

    private:
        friend class AudioEngine;

        void* m_native  = nullptr;  // ALuint source
        bool  m_spatial = true;
    };
} // namespace ID
