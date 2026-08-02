#include "Render/SetParamImpl.hpp"
#include "Resource/ResourceGetter.hpp"

#ifdef IDRENDERER_USE_OPENGL

#include <glad/glad.h>

namespace ID
{
    // float 类型
    void set_param_impl(const PipelineID pipeline, const std::string& param_name, 
        float param1)
    {
        ShaderID shader_id = IDR_ResPipeline(pipeline)->get_shader_id();
        set_param_impl(shader_id, param_name, param1);
    }

    void set_param_impl(const PipelineID pipeline, const std::string& param_name, 
        float param1, float param2)
    {
        ShaderID shader_id = IDR_ResPipeline(pipeline)->get_shader_id();
        set_param_impl(shader_id, param_name, param1, param2);
    }

    void set_param_impl(const PipelineID pipeline, const std::string& param_name, 
        float param1, float param2, float param3)
    {
        ShaderID shader_id = IDR_ResPipeline(pipeline)->get_shader_id();
        set_param_impl(shader_id, param_name, param1, param2, param3);
    }

    void set_param_impl(const PipelineID pipeline, const std::string& param_name, 
        float param1, float param2, float param3, float param4)
    {
        ShaderID shader_id = IDR_ResPipeline(pipeline)->get_shader_id();
        set_param_impl(shader_id, param_name, param1, param2, param3, param4);
    }

    void set_param_impl(const ShaderID shader, const std::string& param_name, 
        float param1)
    {
        Shader* shader_ptr = IDR_ResShader(shader);
        GLint location = shader_ptr->get_uniform_location(param_name);

        if(location != -1)
        {
            glUniform1f(location, param1);
        }
    }

    void set_param_impl(const ShaderID shader, const std::string& param_name, 
        float param1, float param2)
    {
        Shader* shader_ptr = IDR_ResShader(shader);
        GLint location = shader_ptr->get_uniform_location(param_name);

        if(location != -1)
        {
            glUniform2f(location, param1, param2);
        }
    }

    void set_param_impl(const ShaderID shader, const std::string& param_name, 
        float param1, float param2, float param3)
    {
        Shader* shader_ptr = IDR_ResShader(shader);
        GLint location = shader_ptr->get_uniform_location(param_name);

        if(location != -1)
        {
            glUniform3f(location, param1, param2, param3);
        }
    }

    void set_param_impl(const ShaderID shader, const std::string& param_name, 
        float param1, float param2, float param3, float param4)
    {
        Shader* shader_ptr = IDR_ResShader(shader);
        GLint location = shader_ptr->get_uniform_location(param_name);

        if(location != -1)
        {
            glUniform4f(location, param1, param2, param3, param4);
        }
    }

    // int 类型
    void set_param_impl(const PipelineID pipeline, const std::string& param_name, 
        int param1)
    {
        ShaderID shader_id = IDR_ResPipeline(pipeline)->get_shader_id();
        set_param_impl(shader_id, param_name, param1);
    }

    void set_param_impl(const PipelineID pipeline, const std::string& param_name, 
        int param1, int param2)
    {
        ShaderID shader_id = IDR_ResPipeline(pipeline)->get_shader_id();
        set_param_impl(shader_id, param_name, param1, param2);
    }
    
    void set_param_impl(const PipelineID pipeline, const std::string& param_name, 
        int param1, int param2, int param3)
    {
        ShaderID shader_id = IDR_ResPipeline(pipeline)->get_shader_id();
        set_param_impl(shader_id, param_name, param1, param2, param3);
    }


    void set_param_impl(const PipelineID pipeline, const std::string& param_name, 
        int param1, int param2, int param3, int param4)
    {
        ShaderID shader_id = IDR_ResPipeline(pipeline)->get_shader_id();
        set_param_impl(shader_id, param_name, param1, param2, param3, param4);
    }

    void set_param_impl(const ShaderID shader, const std::string& param_name, 
        int param1)
    {
        Shader* shader_ptr = IDR_ResShader(shader);
        GLint location = shader_ptr->get_uniform_location(param_name);

        if(location != -1)
        {
            glUniform1i(location, param1);
        }
    }

    void set_param_impl(const ShaderID shader, const std::string& param_name, 
        int param1, int param2)
    {
        Shader* shader_ptr = IDR_ResShader(shader);
        GLint location = shader_ptr->get_uniform_location(param_name);

        if(location != -1)
        {
            glUniform2i(location, param1, param2);
        }
    }
    
    void set_param_impl(const ShaderID shader, const std::string& param_name, 
        int param1, int param2, int param3)
    {
        Shader* shader_ptr = IDR_ResShader(shader);
        GLint location = shader_ptr->get_uniform_location(param_name);

        if(location != -1)
        {
            glUniform3i(location, param1, param2, param3);
        }
    }

    void set_param_impl(const ShaderID shader, const std::string& param_name, 
        int param1, int param2, int param3, int param4)
    {
        Shader* shader_ptr = IDR_ResShader(shader);
        GLint location = shader_ptr->get_uniform_location(param_name);

        if(location != -1)
        {
            glUniform4i(location, param1, param2, param3, param4);
        }
    }

    // bool 类型
    void set_param_impl(const PipelineID pipeline, const std::string& param_name, 
        bool param1)
    {
        ShaderID shader_id = IDR_ResPipeline(pipeline)->get_shader_id();
        set_param_impl(shader_id, param_name, param1);
    }

    void set_param_impl(const PipelineID pipeline, const std::string& param_name, 
        bool param1, bool param2)
    {
        ShaderID shader_id = IDR_ResPipeline(pipeline)->get_shader_id();
        set_param_impl(shader_id, param_name, param1, param2);
    }

    void set_param_impl(const PipelineID pipeline, const std::string& param_name, 
        bool param1, bool param2, bool param3)
    {
        ShaderID shader_id = IDR_ResPipeline(pipeline)->get_shader_id();
        set_param_impl(shader_id, param_name, param1, param2, param3);
    }

    void set_param_impl(const PipelineID pipeline, const std::string& param_name, 
        bool param1, bool param2, bool param3, bool param4)
    {
        ShaderID shader_id = IDR_ResPipeline(pipeline)->get_shader_id();
        set_param_impl(shader_id, param_name, param1, param2, param3, param4);
    }

    void set_param_impl(const ShaderID shader, const std::string& param_name, 
        bool param1)
    {
        Shader* shader_ptr = IDR_ResShader(shader);
        GLint location = shader_ptr->get_uniform_location(param_name);

        if(location != -1)
        {
            glUniform1i(location, static_cast<int>(param1));
        }
    }

    void set_param_impl(const ShaderID shader, const std::string& param_name, 
        bool param1, bool param2)
    {
        Shader* shader_ptr = IDR_ResShader(shader);
        GLint location = shader_ptr->get_uniform_location(param_name);

        if(location != -1)
        {
            glUniform2i(location, static_cast<int>(param1), static_cast<int>(param2));
        }
    }

    void set_param_impl(const ShaderID shader, const std::string& param_name, 
        bool param1, bool param2, bool param3)
    {
        Shader* shader_ptr = IDR_ResShader(shader);
        GLint location = shader_ptr->get_uniform_location(param_name);

        if(location != -1)
        {
            glUniform3i(location, static_cast<int>(param1), static_cast<int>(param2), static_cast<int>(param3));
        }
    }

    void set_param_impl(const ShaderID shader, const std::string& param_name, 
        bool param1, bool param2, bool param3, bool param4)
    {
        Shader* shader_ptr = IDR_ResShader(shader);
        GLint location = shader_ptr->get_uniform_location(param_name);

        if(location != -1)
        {
            glUniform4i(location, static_cast<int>(param1), static_cast<int>(param2), 
                static_cast<int>(param3), static_cast<int>(param4));
        }
    }

    // IDMat
    void set_param_impl(const PipelineID pipeline, const std::string& param_name, const Mat2& mat)
    {
        ShaderID shader_id = IDR_ResPipeline(pipeline)->get_shader_id();
        set_param_impl(shader_id, param_name, mat);
    }

    void set_param_impl(const PipelineID pipeline, const std::string& param_name, const Mat3& mat)
    {
        ShaderID shader_id = IDR_ResPipeline(pipeline)->get_shader_id();
        set_param_impl(shader_id, param_name, mat);
    }

    void set_param_impl(const PipelineID pipeline, const std::string& param_name, const Mat4& mat)
    {
        ShaderID shader_id = IDR_ResPipeline(pipeline)->get_shader_id();
        set_param_impl(shader_id, param_name, mat);
    }

    void set_param_impl(const ShaderID shader, const std::string& param_name, const Mat2& mat)
    {
        const float* data = mat.get_data();
        RenderCommand::set_param(shader, param_name, 2, 2, data);
    }

    void set_param_impl(const ShaderID shader, const std::string& param_name, const Mat3& mat)
    {
        const float* data = mat.get_data();
        RenderCommand::set_param(shader, param_name, 3, 3, data);
    }
    
    void set_param_impl(const ShaderID shader, const std::string& param_name, const Mat4& mat)
    {
        const float* data = mat.get_data();
        RenderCommand::set_param(shader, param_name, 4, 4, data);
    }
} // namespace ID

namespace ID::RenderCommand
{
    void set_param(const PipelineID pipeline, const std::string& param_name, 
        uint32_t rows, uint32_t cols, const float* data)
    {
        ShaderID shader_id = IDR_ResPipeline(pipeline)->get_shader_id();
        set_param(shader_id, param_name, rows, cols, data);
    }

    void set_param(const ShaderID shader, const std::string& param_name, 
        uint32_t rows, uint32_t cols, const float* data)
    {
        Shader* shader_ptr = IDR_ResShader(shader);
        GLint location = shader_ptr->get_uniform_location(param_name);

        if(location != -1)
        {
            switch(rows)
            {
                case 2:
                    switch(cols)
                    {
                        case 2: glUniformMatrix2fv(location, 1, GL_FALSE, data); break;
                        case 3: glUniformMatrix2x3fv(location, 1, GL_FALSE, data); break;
                        case 4: glUniformMatrix2x4fv(location, 1, GL_FALSE, data); break;
                    }
                    break;
                case 3:
                    switch(cols)
                    {
                        case 2: glUniformMatrix3x2fv(location, 1, GL_FALSE, data); break;
                        case 3: glUniformMatrix3fv(location, 1, GL_FALSE, data); break;
                        case 4: glUniformMatrix3x4fv(location, 1, GL_FALSE, data); break;
                    }
                    break;
                case 4:
                    switch(cols)
                    {
                        case 2: glUniformMatrix4x2fv(location, 1, GL_FALSE, data); break;
                        case 3: glUniformMatrix4x3fv(location, 1, GL_FALSE, data); break;
                        case 4: glUniformMatrix4fv(location, 1, GL_FALSE, data); break;
                    }
                    break;
            }
        }
    }
} // namespace ID::RenderCommand

#endif // IDRENDERER_USE_OPENGL