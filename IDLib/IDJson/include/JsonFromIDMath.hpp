#pragma once

#ifdef IDJSON_SUPPORT_IDMATH

#include "Json.hpp"
#include "IDMath.hpp"

namespace ID::JSON
{
    // 生成 Json 对象，类型为 JSON::Type::Array，包含向量或矩阵的元素
    Json create(const Vec3& vec, ArenaID arena_id);
    Json create(const Vec4& vec, ArenaID arena_id);
    Json create(const Quat& quat, ArenaID arena_id);
    Json create(const Mat3& mat, ArenaID arena_id);
    Json create(const Mat4& mat, ArenaID arena_id);

    // 由 Json 对象解析出向量或矩阵
    template<typename T>
    requires std::same_as<T, Vec3> || std::same_as<T, Vec4> 
        || std::same_as<T, Quat> || std::same_as<T, Mat3> || std::same_as<T, Mat4>
    T parse(const Json& json);

    template<> Vec3 parse<Vec3>(const Json& json);
    template<> Vec4 parse<Vec4>(const Json& json);
    template<> Quat parse<Quat>(const Json& json);
    template<> Mat3 parse<Mat3>(const Json& json);
    template<> Mat4 parse<Mat4>(const Json& json);
} // namespace ID::JSON

#endif