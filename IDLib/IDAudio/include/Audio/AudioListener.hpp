#pragma once

#include "IDAudioCore.hpp"
#include "IDMath.hpp"

namespace ID
{
    /**
     *  @brief 音频监听器（纯数据结构）
     *
     *  OpenAL 只支持一个全局监听器。通常挂载到主摄像机。
     *  AudioEngine 使用此数据设置 OpenAL Listener 属性。
     */
    struct IDAUDIO_API AudioListener
    {
        Vec3 position = Vec3();
        Vec3 forward  = Vec3(0.0f, 0.0f, -1.0f);   // 前方向
        Vec3 up       = Vec3(0.0f, 1.0f, 0.0f);    // 上方向
        Vec3 velocity = Vec3();                     // 速度（用于多普勒效应）

        bool operator==(const AudioListener& other) const
        {
            return position == other.position
                && forward  == other.forward
                && up       == other.up
                && velocity == other.velocity;
        }

        bool operator!=(const AudioListener& other) const { return !(*this == other); }
    };
} // namespace ID
