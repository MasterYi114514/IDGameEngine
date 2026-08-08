#pragma once

#include "IDpch.hpp"

namespace ID
{
    enum class MaterialParamType : uint8_t
    {
        None = 0,
        Int,
        Float,
        Vec2,
        Vec3,
        Vec4,
        Mat3,
        Mat4
    };

    // MaterialParamSupported：支持的材质参数类型
    template<typename T>
    concept MPSupported = std::same_as<T, float> || std::same_as<T, int>
        || std::same_as<T, Vec2> || std::same_as<T, Vec3> || std::same_as<T, Vec4>
        || std::same_as<T, Mat3> || std::same_as<T, Mat4>;

    struct ID_API MaterialParam : public SerializableBase
    {
        MaterialParam() = default;
        MaterialParamType type = MaterialParamType::None;
        Array<float, 16> value;

        template<typename T>
        requires MPSupported<T>
        MaterialParam(T value)
        {
            if constexpr (std::same_as<T, float>)
            {
                this->type = MaterialParamType::Float;
                this->value[0] = value;
            }
            else if constexpr (std::same_as<T, int>)
            {
                this->type = MaterialParamType::Int;
                this->value[0] = static_cast<float>(value);
            }
            else if constexpr (std::same_as<T, Vec2>)
            {
                this->type = MaterialParamType::Vec2;
                this->value[0] = value[0];
                this->value[1] = value[1];
            }
            else if constexpr (std::same_as<T, Vec3>)
            {
                this->type = MaterialParamType::Vec3;
                this->value[0] = value[0];
                this->value[1] = value[1];
                this->value[2] = value[2];
            }
            else if constexpr (std::same_as<T, Vec4>)
            {
                this->type = MaterialParamType::Vec4;
                this->value[0] = value[0];
                this->value[1] = value[1];
                this->value[2] = value[2];
                this->value[3] = value[3];
            }
            else if constexpr (std::same_as<T, Mat3>)
            {
                this->type = MaterialParamType::Mat3;
                std::memcpy(this->value.data, value.get_data(), 9 * sizeof(float));
            }
            else if constexpr (std::same_as<T, Mat4>)
            {
                this->type = MaterialParamType::Mat4;
                std::memcpy(this->value.data, value.get_data(), 16 * sizeof(float));
            }
        }

        bool is_valid() const { return type != MaterialParamType::None; }

        Json serialize(ArenaID arena_id) const override;
        void deserialize(const Json& json) override;
    };
    
} // namespace ID