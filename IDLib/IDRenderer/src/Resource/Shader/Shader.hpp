#pragma once

#include "Core/IDRpch.hpp"
#include "Resource/Shader/ShaderCreateInfo.hpp"
#include "Resource/Shader/ShaderUniformDesc.hpp"

#ifdef IDRENDERER_USE_OPENGL

#include <glad/glad.h>

namespace ID
{
    class Shader
    {
    public:
        Shader() = delete;
        Shader(const ShaderCreateInfo& create_info);
        ~Shader() { destroy(); }

        // 禁止拷贝
        Shader(const Shader&) = delete;
        Shader& operator=(const Shader&) = delete;

        // 允许移动
        Shader(Shader&& other) noexcept;
        Shader& operator=(Shader&& other) noexcept;

        void destroy();

    public:
        GLuint get_program_id() const { return m_program_id; }

        /*
        *   获取 uniform 变量的位置
        *   首次查询会调用 glGetUniformLocation 并缓存结果，后续查询会直接从缓存中获取
        */
        GLint get_uniform_location(const std::string& name) const;

        // 获取 link 后反射出的 active uniform 列表（构造时一次性枚举，按名称排序）
        const std::vector<ShaderUniformDesc>& get_active_uniforms() const { return m_active_uniforms; }

    private:
        GLuint m_program_id = 0;
        mutable std::unordered_map<std::string, GLint> m_uniform_location_cache;
        std::vector<ShaderUniformDesc> m_active_uniforms;
    };
} // namespace ID

#endif