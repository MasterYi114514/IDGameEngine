#pragma once

#include "Core/IDRpch.hpp"
#include "Resource/Shader/ShaderCreateInfo.hpp"

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

    private:
        GLuint m_program_id = 0;
        mutable std::unordered_map<std::string, GLint> m_uniform_location_cache;
    };
} // namespace ID

#endif