#include "Renderer/Material/MaterialParam.hpp"

namespace
{
    constexpr const char* k_param_type_names[] =
    {
        "None",
        "Int",
        "Float",
        "Vec2",
        "Vec3",
        "Vec4",
        "Mat3",
        "Mat4"
    };

    constexpr uint32_t k_param_value_count[] = 
    {
        0, 1, 1, 2, 3, 4, 9, 16
    };

    ID::MaterialParamType type_from_name(const std::string& name)
    {
        for (uint8_t i = 0; i < 9; ++i)
        {
            if (name == k_param_type_names[i])
                return static_cast<ID::MaterialParamType>(i);
        }
        return ID::MaterialParamType::None;
    }
} // 匿名命名空间

namespace ID
{
    Json MaterialParam::serialize(ArenaID arena_id) const
    {
        Json json = Json::create_object(arena_id);
        uint32_t idx = static_cast<uint8_t>(type);
        json.insert("type", Json::create_string(k_param_type_names[idx], arena_id));

        Json jvalue = Json::create_array(arena_id);
        for(uint32_t i = 0; i < k_param_value_count[idx]; ++i)
        {
            jvalue.push_back(Json(static_cast<double>(value[i])));
        }
        json.insert("value", jvalue);
        return json;
    }

    void MaterialParam::deserialize(const Json& json)
    {
        if(!json.is_object()) return;

        type = type_from_name(json["type"].as_cstr());
        uint32_t idx = static_cast<uint8_t>(type);
        for(uint32_t i = 0; i < k_param_value_count[idx]; ++i)
        {
            value[i] = static_cast<float>(json["value"][i].as_float());
        }
    }
}