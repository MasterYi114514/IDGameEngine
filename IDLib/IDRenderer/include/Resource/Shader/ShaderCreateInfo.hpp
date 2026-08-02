#pragma once

#include "Core/IDRpch.hpp"

namespace ID
{
    struct IDR_API ShaderCreateInfo
    {
        ShaderCreateInfo() = delete;

        ShaderCreateInfo(const std::string& vs_source, const std::string& fs_source)
            : vs_source(vs_source), fs_source(fs_source) { }

        std::string vs_source;
        std::string fs_source;
    };
} // namespace ID