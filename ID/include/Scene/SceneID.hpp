#pragma once

#include "IDpch.hpp"

namespace ID
{
    /*
    *   SceneID 是场景的运行时唯一标识（SceneManager 递增分配）。
    *   名字只是名字，可随时修改；场景身份由 SceneID 决定。
    *   默认构造为 INVALID，表示无效 ID。
    */
    struct ID_API SceneID
    {
        using UnderlyingType = uint32_t;
        static constexpr UnderlyingType INVALID = static_cast<UnderlyingType>(-1);

        UnderlyingType id = INVALID;

        bool is_valid() const { return id != INVALID; }
        bool operator==(const SceneID& other) const { return id == other.id; }
        bool operator!=(const SceneID& other) const { return id != other.id; }

        struct Hash
        {
            size_t operator()(const SceneID& sid) const
            {
                return std::hash<UnderlyingType>()(sid.id);
            }
        };
    };
} // namespace ID
