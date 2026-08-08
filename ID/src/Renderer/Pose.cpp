#include "Renderer/Pose.hpp"
#include "Log/Log.hpp"

namespace ID
{
    Mat4 Pose<OrientationDescType::Quaternion>::get_transform_matrix() const
    {
        // T * R：平移 * 旋转，不包含缩放
        return Math::get_translation(position) * orientation.to_mat4();
    }

    Json Pose<OrientationDescType::Quaternion>::serialize(ArenaID arena_id) const
    {
        Json json = Json::create_object(arena_id);
        json.insert("position", JSON::create(position, arena_id));
        json.insert("ori(Quat)", JSON::create(orientation, arena_id));
        return json;
    }

    void Pose<OrientationDescType::Quaternion>::deserialize(const Json& json)
    {
        if(!json.is_object())
        {
            ID_WARN("Pose<Quat>::deserialize: 尝试把非对象 JSON 解析为 Pose<Quat>");
            return;
        }

        position = JSON::parse<Pos3>(json["position"]);
        orientation = JSON::parse<Quat>(json["ori(Quat)"]);
    }

    Mat4 Pose<OrientationDescType::FrontUp>::get_transform_matrix() const
    {
        // 构建正交基：right = front × up, corrected_up = right × front
        Vec3 r = Math::cross(front, up);
        r.normalize();
        Vec3 u = Math::cross(r, front);

        Mat4 rot = Math::get_identity_mat4();
        // 列主序旋转矩阵：列0=right, 列1=up, 列2=-front
        rot.element(0, 0) = r[0]; rot.element(0, 1) = u[0]; rot.element(0, 2) = -front[0];
        rot.element(1, 0) = r[1]; rot.element(1, 1) = u[1]; rot.element(1, 2) = -front[1];
        rot.element(2, 0) = r[2]; rot.element(2, 1) = u[2]; rot.element(2, 2) = -front[2];

        // T * R
        return Math::get_translation(position) * rot;
    }

    Json Pose<OrientationDescType::FrontUp>::serialize(ArenaID arena_id) const
    {
        Json json = Json::create_object(arena_id);
        json.insert("position", JSON::create(position, arena_id));
        json.insert("front", JSON::create(front, arena_id));
        json.insert("up", JSON::create(up, arena_id));
        return json;
    }

    void Pose<OrientationDescType::FrontUp>::deserialize(const Json& json)
    {
        if(!json.is_object())
        {
            ID_WARN("Pose<FrontUp>::deserialize: 尝试把非对象 JSON 解析为 Pose<FrontUp>");
            return;
        }

        position = JSON::parse<Pos3>(json["position"]);
        front = JSON::parse<Vec3>(json["front"]);
        up = JSON::parse<Vec3>(json["up"]);
    }
}