#pragma once

#include "Core/IDRpch.hpp"

#include <filesystem>

namespace ID
{
    /*
    *   IDRenderer 提供的 ShaderSourceLoader 工具类，不允许实例化
    */
    class IDR_API ShaderSourceLoader
    {
    public:
        ShaderSourceLoader() = delete;

        static std::string load_shader_source(const std::filesystem::path& shader_path);
    };
} // namespace ID