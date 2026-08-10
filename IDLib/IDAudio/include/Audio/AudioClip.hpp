#pragma once

#include "IDAudioCore.hpp"

#include <cstdint>
#include <functional>

namespace ID
{
    struct AudioData;   // 前向声明（定义在 IDAsset：Loader/AudioLoader.hpp）
    /**
     *  @brief 音频片段句柄（槽位下标）
     */
    struct IDAUDIO_API AudioClipID
    {
        using UnderlyingType = uint32_t;
        static constexpr UnderlyingType INVALID = static_cast<UnderlyingType>(-1);

        UnderlyingType id = INVALID;

        bool is_valid() const { return id != INVALID; }
        bool operator==(const AudioClipID& other) const { return id == other.id; }
        bool operator!=(const AudioClipID& other) const { return id != other.id; }

        struct Hash
        {
            size_t operator()(const AudioClipID& cid) const
            {
                return std::hash<UnderlyingType>()(cid.id);
            }
        };
    };

    /**
     *  @brief 音频片段（只读音频数据）
     *
     *  封装 OpenAL Buffer。存储解码后的 PCM 数据。
     *  遵循"空壳模式"：不可拷贝，可移动。
     *
     *  Phase 6 支持格式：WAV（PCM）。
     *  OGG Vorbis 格式延后（需要 stb_vorbis 或 libvorbis）。
     */
    class IDAUDIO_API AudioClip
    {
    public:
        AudioClip() = default;
        ~AudioClip() { destroy(); }

        // 不可拷贝
        AudioClip(const AudioClip&) = delete;
        AudioClip& operator=(const AudioClip&) = delete;

        // 可移动
        AudioClip(AudioClip&& other) noexcept;
        AudioClip& operator=(AudioClip&& other) noexcept;

        /**
         *  @brief 释放 OpenAL Buffer，对象变空壳
         */
        void destroy();

        // ========== 加载 ==========

        /**
         *  @brief 从已解码的 AudioData 创建音频片段
         *  @param data 解码后的 PCM 数据（由 IDAsset 的 AudioLoader 提供）
         *  @return true=创建成功
         *
         *  文件 IO + 解析由 IDAsset 负责，IDAudio 只负责提交到 OpenAL。
         *  重复调用会先释放旧 buffer，无泄漏。
         */
        bool create_from_audio_data(const AudioData& data);

        bool is_valid() const { return m_native != nullptr; }

        // ========== 属性（只读） ==========

        float    get_duration()    const { return m_duration; }
        uint32_t get_sample_rate() const { return m_sample_rate; }
        uint8_t  get_channels()    const { return m_channels; }

        // ========== 内部 ==========

        void* get_native_handle() const { return m_native; }

    private:
        friend class AudioEngine;

        void*    m_native       = nullptr;  // ALuint buffer
        float    m_duration     = 0.0f;
        uint32_t m_sample_rate  = 0;
        uint8_t  m_channels     = 0;
    };
} // namespace ID
