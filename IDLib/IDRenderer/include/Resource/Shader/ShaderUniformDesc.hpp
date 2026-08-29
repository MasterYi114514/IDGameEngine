#pragma once

#include "Core/IDRpch.hpp"

namespace ID
{
    /*
    *   后端无关的 uniform 类型抽象（公开头文件，禁止依赖 glad / vulkan 等平台头文件）
    */
    enum class ShaderUniformType : uint8_t
    {
        None = 0,
        Float,
        Int,
        Bool,
        Vec2,
        Vec3,
        Vec4,
        Mat3,
        Mat4,
        Sampler2D,
        SamplerCube,
        Unsupported
    };

    /*
    *   单个 active uniform 的反射描述（Shader link 后一次性枚举生成）
    */
    struct IDR_API ShaderUniformDesc
    {
        std::string         name;                              // 数组已截断 "[0]" 的基础名
        ShaderUniformType   type = ShaderUniformType::None;
        uint32_t            count = 1;                         // >1 表示数组
    };
} // namespace ID
