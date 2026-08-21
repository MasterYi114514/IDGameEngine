#pragma once

#include "IDAudioCore.hpp"
#include "IDMath.hpp"
#include "Audio/AudioClip.hpp"
#include "Audio/AudioSource.hpp"
#include "Audio/AudioListener.hpp"

namespace ID
{
    struct AudioData;   // 前向声明（定义在 IDAsset：Loader/AudioLoader.hpp）

    /**
     *  @brief 纯静态音频引擎
     *
     *  封装 OpenAL 设备 + 上下文。所有方法为 static。
     *  使用前必须调用 init()，使用后调用 shutdown()。
     *
     *  资源池管理：
     *  - AudioSource 池：create_source / destroy_source / get_source
     *  - AudioClip 池：create_clip / unload_clip / get_clip
     */
    class IDAUDIO_API AudioEngine
    {
    public:
        AudioEngine() = delete;
        ~AudioEngine() = delete;

        // ========== 生命周期 ==========

        /**
         *  @brief 初始化音频引擎
         *  @return true=成功
         *
         *  打开默认音频设备 + 创建 OpenAL 上下文。
         *  重复调用安全（幂等）。
         */
        static bool init();

        /**
         *  @brief 关闭音频引擎
         *
         *  销毁所有 AudioSource + AudioClip，释放设备和上下文。
         */
        static void shutdown();

        /**
         *  @brief 检查是否已初始化
         */
        static bool is_initialized();

        // ========== 监听器（全局唯一） ==========

        /**
         *  @brief 设置监听器位置
         */
        static void set_listener_position(const Vec3& position);

        /**
         *  @brief 设置监听器朝向
         *  @param forward 前方向（通常为摄像机 -Z 方向）
         *  @param up      上方向（通常为世界 Y 轴）
         */
        static void set_listener_orientation(const Vec3& forward, const Vec3& up);

        /**
         *  @brief 设置监听器速度（用于多普勒效应）
         */
        static void set_listener_velocity(const Vec3& velocity);

        /**
         *  @brief 一次性设置所有监听器属性
         */
        static void set_listener(const AudioListener& listener);

        // ========== 全局控制 ==========

        /**
         *  @brief 设置主音量
         *  @param volume 0.0（静音）~ 1.0（最大）
         */
        static void  set_master_volume(float volume);
        static float get_master_volume();

        // ========== 音源管理 ==========

        /**
         *  @brief 创建新音源
         *  @return 音源句柄
         */
        static AudioSourceID create_source();

        /**
         *  @brief 销毁音源
         */
        static void destroy_source(AudioSourceID id);

        /**
         *  @brief 获取音源
         */
        static AudioSource& get_source(AudioSourceID id);

        // ========== 音频片段管理 ==========

        /**
         *  @brief 从已解码的 AudioData 创建音频片段
         *  @param data 解码后的 PCM 数据（由 IDAsset 的 AudioLoader 提供）
         *  @return 片段句柄（INVALID = 创建失败）
         *
         *  文件 IO + 解析由 IDAsset 负责；去重/缓存职责已移交 AssetLibrary，
         *  此处不强制去重，重复调用会分配独立槽位。
         */
        static AudioClipID create_clip(const AudioData& data);

        /**
         *  @brief 卸载音频片段
         */
        static void unload_clip(AudioClipID id);

        /**
         *  @brief 获取音频片段
         */
        static AudioClip& get_clip(AudioClipID id);

        // ========== 每帧更新 ==========

        /**
         *  @brief 每帧调用（处理流式音频缓冲等）
         */
        static void on_update();
    };
} // namespace ID
