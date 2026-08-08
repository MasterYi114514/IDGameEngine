#pragma once

#include "IDpch.hpp"
#include "IDMath.hpp"

namespace ID
{
    enum class LightType : uint8_t
    {
        Directional = 0,            // 平行光
        Point,                      // 点光源
        Spot,                       // 聚光灯
    };

    struct ID_API Light : public SerializableBase
    {
        LightType type = LightType::Directional;    // 光源类型

        Vec3  color = Vec3(1.0f, 1.0f, 1.0f);       // 光源颜色
        float intensity = 1.0f;                     // 光源强度

        union
        {
            Vec3 direction;           // 平行光方向（仅在 type 为 Directional 时有效）
            Pos3 position;            // 点光源或聚光灯位置（仅在 type 为 Point 或 Spot 时有效）
        }drop;          // d or p 的通假

        // 仅在 type == LightType::Spot 时有效
        float inner_cone_angle = 15.0f;         // 聚光灯内锥角（单位：度）
        float outer_cone_angle = 30.0f;         // 聚光灯外锥角（单位：度）

        bool enabled = true;                    // 光源是否启用
        uint8_t  _pad[3] = { 0 };               // std140 对齐填充

        // 序列化
        Json serialize(ArenaID arena_id) const override;
        void deserialize(const Json& json) override;
    };
} // namespace ID