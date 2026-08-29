#pragma once

#include "IDpch.hpp"
#include "Renderer/IDRCore.hpp"

namespace ID
{
    /**
     *  在 ID 内部，ShaderManager 会覆盖全局的 ShaderManager
     */
    class ID_API ShaderManager
    {
    public:
        ShaderManager() = delete;
        ~ShaderManager() = delete;
    public:
        static ShaderID create(const std::string& vertex_path, const std::string& fragment_path);
        static std::string get_vertex_shader_path(ShaderID shader_id);
        static std::string get_fragment_shader_path(ShaderID shader_id);

        // 获取 shader link 后反射出的 active uniform 列表（无效 ID 返回空表）
        static std::vector<ShaderUniformDesc> get_active_uniforms(ShaderID shader_id);

        static Json serialize_shader(ShaderID shader_id, ArenaID arena_id);
        static ShaderID deserialize_shader(const Json& json);
    };
} // namespace ID