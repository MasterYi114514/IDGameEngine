#pragma once

#include "IDpch.hpp"

namespace ID
{
    using AudioUINT = uint32_t;

    /*
    *   AudioID 是 ID 引擎层的音频资源句柄（槽位下标）。
    *   与 IDAudio 库的 AudioClipID 解耦：对外组件/Panel 使用 AudioID，
    *   底层 OpenAL 句柄（AudioClipID）由 AudioManager 内部维护映射。
    *   成员 id 默认值为 INVALID，表示无效句柄。
    */
    struct ID_API AudioID
    {
        using UnderlyingType = AudioUINT;
        static constexpr UnderlyingType INVALID = static_cast<UnderlyingType>(-1);

        UnderlyingType id = INVALID;

        bool is_valid() const { return id != INVALID; }
        bool operator==(const AudioID& other) const { return id == other.id; }
        bool operator!=(const AudioID& other) const { return id != other.id; }

        struct Hash
        {
            size_t operator()(const AudioID& aid) const
            {
                return std::hash<UnderlyingType>()(aid.id);
            }
        };
    };
} // namespace ID
