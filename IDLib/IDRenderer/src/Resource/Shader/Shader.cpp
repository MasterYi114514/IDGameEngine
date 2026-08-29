#include "Resource/Shader/Shader.hpp"
#include "Log/Log.hpp"
#include <filesystem>
#include <algorithm>

#ifdef IDRENDERER_USE_OPENGL

namespace
{
    // GL 枚举 → 后端无关的 ShaderUniformType 映射，未知类型返回 Unsupported
    ID::ShaderUniformType gl_type_to_uniform_type(GLenum gl_type)
    {
        switch(gl_type)
        {
            case GL_FLOAT:          return ID::ShaderUniformType::Float;
            case GL_INT:            return ID::ShaderUniformType::Int;
            case GL_BOOL:           return ID::ShaderUniformType::Bool;
            case GL_FLOAT_VEC2:     return ID::ShaderUniformType::Vec2;
            case GL_FLOAT_VEC3:     return ID::ShaderUniformType::Vec3;
            case GL_FLOAT_VEC4:     return ID::ShaderUniformType::Vec4;
            case GL_FLOAT_MAT3:     return ID::ShaderUniformType::Mat3;
            case GL_FLOAT_MAT4:     return ID::ShaderUniformType::Mat4;
            case GL_SAMPLER_2D:     return ID::ShaderUniformType::Sampler2D;
            case GL_SAMPLER_CUBE:   return ID::ShaderUniformType::SamplerCube;
            default:                return ID::ShaderUniformType::Unsupported;
        }
    }

    // 枚举 program 的全部 active uniform（link 后调用，一次性反射）：
    //   跳过：UBO block 成员（名字含 '.'）、gl_ 内建、映射后 Unsupported 的类型；
    //   数组名截断 "[0]" 后缀并记录 count；结果按名称字典序排序保证 UI 顺序稳定
    std::vector<ID::ShaderUniformDesc> collect_active_uniforms(GLuint program)
    {
        std::vector<ID::ShaderUniformDesc> uniforms;

        GLint uniform_count = 0;
        glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &uniform_count);

        for(GLint i = 0; i < uniform_count; ++i)
        {
            char name_buf[256];
            GLsizei name_len = 0;
            GLint   size = 0;
            GLenum  gl_type = 0;
            glGetActiveUniform(program, static_cast<GLuint>(i), sizeof(name_buf),
                &name_len, &size, &gl_type, name_buf);

            std::string name(name_buf, static_cast<size_t>(name_len));

            // UBO block 成员形如 "BlockName.member"，不属于普通 uniform，跳过
            if(name.find('.') != std::string::npos) continue;
            // gl_ 内建保险（正常不会被列为 active，双保险）
            if(name.rfind("gl_", 0) == 0) continue;

            const ID::ShaderUniformType type = gl_type_to_uniform_type(gl_type);
            if(type == ID::ShaderUniformType::Unsupported) continue;

            // 数组 uniform 名字带 "[0]" 后缀，截断出基础名
            const size_t bracket = name.find('[');
            if(bracket != std::string::npos)
            {
                name = name.substr(0, bracket);
            }

            uniforms.push_back({ name, type, static_cast<uint32_t>(size) });
        }

        std::sort(uniforms.begin(), uniforms.end(),
            [](const ID::ShaderUniformDesc& a, const ID::ShaderUniformDesc& b)
            { return a.name < b.name; });

        return uniforms;
    }

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

        // link 成功后一次性反射 active uniforms（失败时 program 为 0，反射得空表）
        m_active_uniforms = collect_active_uniforms(m_program_id);
    }

    void Shader::destroy()
    {
        if(m_program_id != 0)
        {
            glDeleteProgram(m_program_id);
            m_program_id = 0;
        }

        m_uniform_location_cache.clear();
        m_active_uniforms.clear();
    }

    Shader::Shader(Shader&& other) noexcept : m_program_id(other.m_program_id), m_uniform_location_cache(std::move(other.m_uniform_location_cache)), m_active_uniforms(std::move(other.m_active_uniforms))
    {
        other.m_program_id = 0;
    }

    Shader& Shader::operator=(Shader&& other) noexcept
    {
        if(this != &other)
        {
            std::swap(m_program_id, other.m_program_id);
            std::swap(m_uniform_location_cache, other.m_uniform_location_cache);
            std::swap(m_active_uniforms, other.m_active_uniforms);

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
