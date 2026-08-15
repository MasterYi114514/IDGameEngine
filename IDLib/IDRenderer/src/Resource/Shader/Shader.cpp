#include "Resource/Shader/Shader.hpp"
#include "Log/Log.hpp"
#include <filesystem>

#ifdef IDRENDERER_USE_OPENGL

namespace
{
    GLuint compile_vs(const std::string& source)
    {
        GLuint vs = glCreateShader(GL_VERTEX_SHADER);
        const char* source_cstr = source.c_str();
        glShaderSource(vs, 1, &source_cstr, nullptr);
        glCompileShader(vs);

        int success;
        glGetShaderiv(vs, GL_COMPILE_STATUS, &success);
        if(!success)
        {
            char info_log[1024];
            glGetShaderInfoLog(vs, 1024, nullptr, info_log);

            IDR_ERROR("VertexShader 编译失败: {}", info_log);
        }

        return vs;
    }

    GLuint compile_fs(const std::string& source)
    {
        GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
        const char* source_cstr = source.c_str();
        glShaderSource(fs, 1, &source_cstr, nullptr);
        glCompileShader(fs);

        int success;
        glGetShaderiv(fs, GL_COMPILE_STATUS, &success);
        if(!success)
        {
            char info_log[1024];
            glGetShaderInfoLog(fs, 1024, nullptr, info_log);

            IDR_ERROR("FragmentShader 编译失败: {}", info_log);
        }

        return fs;
    }

    GLuint attach_and_link(GLuint vs, GLuint fs)
    {
        GLuint program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glLinkProgram(program);

        int success;
        glGetProgramiv(program, GL_LINK_STATUS, &success);
        if(!success)
        {
            char info_log[1024];
            glGetProgramInfoLog(program, 1024, nullptr, info_log);

            IDR_ERROR("Shader Program 链接失败: {}", info_log);
        }

        return program;
    }
} // 匿名命名空间

namespace ID
{
    Shader::Shader(const ShaderCreateInfo& create_info) : m_program_id(0)
    {
        GLuint vs = compile_vs(create_info.vs_source);
        GLuint fs = compile_fs(create_info.fs_source);

        m_program_id = attach_and_link(vs, fs);
    }

    void Shader::destroy()
    {
        if(m_program_id != 0)
        {
            glDeleteProgram(m_program_id);
            m_program_id = 0;
        }

        m_uniform_location_cache.clear();
    }

    Shader::Shader(Shader&& other) noexcept : m_program_id(other.m_program_id), m_uniform_location_cache(std::move(other.m_uniform_location_cache))
    {
        other.m_program_id = 0;
    }

    Shader& Shader::operator=(Shader&& other) noexcept
    {
        if(this != &other)
        {
            std::swap(m_program_id, other.m_program_id);
            std::swap(m_uniform_location_cache, other.m_uniform_location_cache);

            other.destroy();
        }

        return *this;
    }

    GLint Shader::get_uniform_location(const std::string& name) const
    {
        auto it = m_uniform_location_cache.find(name);
        if(it != m_uniform_location_cache.end())
        {
            return it->second;
        }

        GLint location = glGetUniformLocation(m_program_id, name.c_str());
        if(location == -1)
        {
            // 设置不存在的 uniform 是 OpenGL 合法 no-op（如 geometry shader 无 u_view/u_proj），
            // 属正常高频路径，降为 TRACE 避免刷屏；真拼写错误由开发者调试时观察
            IDR_TRACE("Uniform 变量 {} 不存在或未被使用", name);
        }

        m_uniform_location_cache[name] = location;
        return location;
    }
} // namespace ID

#endif
