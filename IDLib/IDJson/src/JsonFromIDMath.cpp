#ifdef IDJSON_SUPPORT_IDMATH
#include "JsonFromIDMath.hpp"
#include "Log.hpp"

namespace ID::JSON
{
    Json create(const Vec3& vec, ArenaID arena_id)
    {
        Json json = Json::create_array(arena_id);
        json.push_back(Json(static_cast<double>(vec[0])));
        json.push_back(Json(static_cast<double>(vec[1])));
        json.push_back(Json(static_cast<double>(vec[2])));
        return json;
    }

    Json create(const Vec4& vec, ArenaID arena_id)
    {
        Json json = Json::create_array(arena_id);
        json.push_back(Json(static_cast<double>(vec[0])));
        json.push_back(Json(static_cast<double>(vec[1])));
        json.push_back(Json(static_cast<double>(vec[2])));
        json.push_back(Json(static_cast<double>(vec[3])));
        return json;
    }

    Json create(const Quat& quat, ArenaID arena_id)
    {
        Json json = Json::create_array(arena_id);
        json.push_back(Json(static_cast<double>(quat[0])));
        json.push_back(Json(static_cast<double>(quat[1])));
        json.push_back(Json(static_cast<double>(quat[2])));
        json.push_back(Json(static_cast<double>(quat[3])));
        return json;
    }

    Json create(const Mat3& mat, ArenaID arena_id)
    {
        Json json = Json::create_array(arena_id);
        for (std::size_t i = 0; i < 3; ++i)
        {
            for (std::size_t j = 0; j < 3; ++j)
            {
                json.push_back(Json(static_cast<double>(mat[i][j])));
            }
        }
        return json;
    }

    Json create(const Mat4& mat, ArenaID arena_id)
    {
        Json json = Json::create_array(arena_id);
        for (std::size_t i = 0; i < 4; ++i)
        {
            for (std::size_t j = 0; j < 4; ++j)
            {
                json.push_back(Json(static_cast<double>(mat[i][j])));
            }
        }
        return json;
    }

    template<>
    Vec3 parse<Vec3>(const Json& json)
    {
        if(!json.is_array() || json.size() != 3)
        {
            IDJSON_WARN("JSON::parse<Vec3>: 尝试把非 Vec3 的 JSON 解析为 Vec3");
            return Vec3{};
        }

        Vec3 vec;
        vec[0] = static_cast<float>(json[0].as_float());
        vec[1] = static_cast<float>(json[1].as_float());
        vec[2] = static_cast<float>(json[2].as_float());
        return vec;
    }

    template<>
    Vec4 parse<Vec4>(const Json& json)
    {
        if(!json.is_array() || json.size() != 4)
        {
            IDJSON_WARN("JSON::parse<Vec4>: 尝试把非 Vec4 的 JSON 解析为 Vec4");
            return Vec4{};
        }

        Vec4 vec;
        vec[0] = static_cast<float>(json[0].as_float());
        vec[1] = static_cast<float>(json[1].as_float());
        vec[2] = static_cast<float>(json[2].as_float());
        vec[3] = static_cast<float>(json[3].as_float());
        return vec;
    }

    template<>
    Quat parse<Quat>(const Json& json)
    {
        if(!json.is_array() || json.size() != 4)
        {
            IDJSON_WARN("JSON::parse<Quat>: 尝试把非 Quat 的 JSON 解析为 Quat");
            return Quat{};
        }

        Quat quat;
        quat[0] = static_cast<float>(json[0].as_float());
        quat[1] = static_cast<float>(json[1].as_float());
        quat[2] = static_cast<float>(json[2].as_float());
        quat[3] = static_cast<float>(json[3].as_float());
        return quat;
    }

    template<>
    Mat3 parse<Mat3>(const Json& json)
    {
        if(!json.is_array() || json.size() != 9)
        {
            IDJSON_WARN("JSON::parse<Mat3>: 尝试把非 Mat3 的 JSON 解析为 Mat3");
            return Mat3{};
        }

        Mat3 mat;
        for (std::size_t i = 0; i < 3; ++i)
        {
            for (std::size_t j = 0; j < 3; ++j)
            {
                mat[i][j] = static_cast<float>(json[i * 3 + j].as_float());
            }
        }
        return mat;
    }

    template<>
    Mat4 parse<Mat4>(const Json& json)
    {
        if(!json.is_array() || json.size() != 16)
        {
            IDJSON_WARN("JSON::parse<Mat4>: 尝试把非 Mat4 的 JSON 解析为 Mat4");
            return Mat4{};
        }
        
        Mat4 mat;
        for (std::size_t i = 0; i < 4; ++i)
        {
            for (std::size_t j = 0; j < 4; ++j)
            {
                mat[i][j] = static_cast<float>(json[i * 4 + j].as_float());
            }
        }
        return mat;
    }
} // namespace ID::JSON

#endif