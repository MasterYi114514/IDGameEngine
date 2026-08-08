#include "Renderer/Light/Light.hpp"

#include "IDJson.hpp"
#include "Log/Log.hpp"

namespace
{
    std::string to_string(ID::LightType type)
    {
        switch(type)
        {
            case ID::LightType::Directional: return "Directional";
            case ID::LightType::Point:       return "Point";
            case ID::LightType::Spot:        return "Spot";
            default:                         return "Unknown";
        }
    }

    ID::LightType light_type_from_string(const std::string& str)
    {
        if(str == "Directional") return ID::LightType::Directional;
        if(str == "Point")       return ID::LightType::Point;
        if(str == "Spot")        return ID::LightType::Spot;
        return ID::LightType::Directional; // 默认值
    }
} // 匿名命名空间

namespace ID
{
    Json Light::serialize(ArenaID arena_id) const
    {
        Json result = Json::create_object(arena_id);
        result.insert("type", Json::create_string(to_string(type), arena_id));
        result.insert("color", JSON::create(color, arena_id));
        result.insert("intensity", Json(static_cast<double>(intensity)));
        result.insert("inner_cone_angle", Json(static_cast<double>(inner_cone_angle)));
        result.insert("outer_cone_angle", Json(static_cast<double>(outer_cone_angle)));

        Json jdrop = Json::create_array(arena_id);
        if(type == LightType::Directional)
        {
            jdrop = JSON::create(drop.direction, arena_id);
        }
        else if(type == LightType::Point || type == LightType::Spot)
        {
            jdrop = JSON::create(drop.position, arena_id);
        }
        result.insert("drop", jdrop);
        
        return result;
    }

    void Light::deserialize(const Json& json)
    {
        if(!json.is_object())
        {
            ID_ERROR("Light::deserialize：尝试把非对象 Json 反序列化为 Light");
            return;
        }

        // type 可能是 ShortString（"Point"/"Spot" ≤ 7 字节），is_string() 判断不到，必须两者都认
        const Json& type_json = json["type"];
        if (type_json.is_string() || type_json.is_sstr())
        {
            type = light_type_from_string(type_json.as_cstr());
        }
        else
        {
            ID_ERROR("Light::deserialize：缺少或无效的 'type' 字段");
        }

        color = JSON::parse<Vec3>(json["color"]);

        // is_float() 不够：旧文件可能把整数值浮点写成 Int（"1" 格式）
        if (json["intensity"].is_float() || json["intensity"].is_int())
        {
            intensity = static_cast<float>(json["intensity"].as_float());
        }
        else
        {
            ID_ERROR("Light::deserialize：缺少或无效的 'intensity' 字段");
        }

        // Bug 修复：聚光灯锥角（旧文件缺失时保持默认值 15/30 度）
        if (json.contains("inner_cone_angle"))
        {
            const Json& inner_cone = json["inner_cone_angle"];
            if (inner_cone.is_float() || inner_cone.is_int())
            {
                inner_cone_angle = static_cast<float>(inner_cone.as_float());
            }
        }

        if (json.contains("outer_cone_angle"))
        {
            const Json& outer_cone = json["outer_cone_angle"];
            if (outer_cone.is_float() || outer_cone.is_int())
            {
                outer_cone_angle = static_cast<float>(outer_cone.as_float());
            }
        }

        switch(type)
        {
            case LightType::Directional:
            {
                drop.direction = JSON::parse<Vec3>(json["drop"]);
                break;
            }
            case LightType::Point:
            case LightType::Spot:
            {
                drop.position = JSON::parse<Pos3>(json["drop"]);
                break;
            }
        }
    }
} // namespace ID