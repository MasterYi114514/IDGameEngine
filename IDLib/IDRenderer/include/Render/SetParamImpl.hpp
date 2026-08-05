#pragma once

#include "Render/RenderCommand.hpp"

namespace ID
{
    // float 类型
    void IDR_API set_param_impl(const PipelineID pipeline, const std::string& param_name, 
        float param1);
    void IDR_API set_param_impl(const PipelineID pipeline, const std::string& param_name, 
        float param1, float param2);
    void IDR_API set_param_impl(const PipelineID pipeline, const std::string& param_name, 
        float param1, float param2, float param3);
    void IDR_API set_param_impl(const PipelineID pipeline, const std::string& param_name, 
        float param1, float param2, float param3, float param4);

    void IDR_API set_param_impl(const ShaderID shader, const std::string& param_name, 
        float param1);
    void IDR_API set_param_impl(const ShaderID shader, const std::string& param_name, 
        float param1, float param2);
    void IDR_API set_param_impl(const ShaderID shader, const std::string& param_name, 
        float param1, float param2, float param3);
    void IDR_API set_param_impl(const ShaderID shader, const std::string& param_name, 
        float param1, float param2, float param3, float param4);

    // int 类型
    void IDR_API set_param_impl(const PipelineID pipeline, const std::string& param_name, 
        int param1);
    void IDR_API set_param_impl(const PipelineID pipeline, const std::string& param_name, 
        int param1, int param2);
    void IDR_API set_param_impl(const PipelineID pipeline, const std::string& param_name, 
        int param1, int param2, int param3);
    void IDR_API set_param_impl(const PipelineID pipeline, const std::string& param_name, 
        int param1, int param2, int param3, int param4);

    void IDR_API set_param_impl(const ShaderID shader, const std::string& param_name, 
        int param1);
    void IDR_API set_param_impl(const ShaderID shader, const std::string& param_name, 
        int param1, int param2);
    void IDR_API set_param_impl(const ShaderID shader, const std::string& param_name, 
        int param1, int param2, int param3);
    void IDR_API set_param_impl(const ShaderID shader, const std::string& param_name, 
        int param1, int param2, int param3, int param4);

    // bool 类型
    void IDR_API set_param_impl(const PipelineID pipeline, const std::string& param_name, 
        bool param1);
    void IDR_API set_param_impl(const PipelineID pipeline, const std::string& param_name, 
        bool param1, bool param2);
    void IDR_API set_param_impl(const PipelineID pipeline, const std::string& param_name, 
        bool param1, bool param2, bool param3);
    void IDR_API set_param_impl(const PipelineID pipeline, const std::string& param_name, 
        bool param1, bool param2, bool param3, bool param4);

    void IDR_API set_param_impl(const ShaderID shader, const std::string& param_name, 
        bool param1);
    void IDR_API set_param_impl(const ShaderID shader, const std::string& param_name, 
        bool param1, bool param2);
    void IDR_API set_param_impl(const ShaderID shader, const std::string& param_name, 
        bool param1, bool param2, bool param3);
    void IDR_API set_param_impl(const ShaderID shader, const std::string& param_name, 
        bool param1, bool param2, bool param3, bool param4);

    // IDMat
    void IDR_API set_param_impl(const PipelineID pipeline, const std::string& param_name, const Mat2& mat);
    void IDR_API set_param_impl(const PipelineID pipeline, const std::string& param_name, const Mat3& mat);
    void IDR_API set_param_impl(const PipelineID pipeline, const std::string& param_name, const Mat4& mat); 
    void IDR_API set_param_impl(const ShaderID shader, const std::string& param_name, const Mat2& mat);
    void IDR_API set_param_impl(const ShaderID shader, const std::string& param_name, const Mat3& mat);
    void IDR_API set_param_impl(const ShaderID shader, const std::string& param_name, const Mat4& mat); 

} // namespace ID

namespace ID::RenderCommand
{
    template<typename... Args>
    requires ParamConcept::CanSetParam<Args...>
    void set_param(const PipelineID pipeline, const std::string& param_name, const Args&... args)
    {
        bind_pipeline(pipeline);

        if constexpr (ParamConcept::AllFloat<Args...>)
        {
            set_param_impl(pipeline, param_name, static_cast<float>(args)...);
        }
        else if constexpr (ParamConcept::AllInt<Args...>)
        {
            set_param_impl(pipeline, param_name, static_cast<int>(args)...);
        }
        else if constexpr (ParamConcept::AllBool<Args...>)
        {
            set_param_impl(pipeline, param_name, static_cast<bool>(args)...);
        }
    }

    template<typename... Args>
    requires ParamConcept::CanSetParam<Args...>
    void set_param(const ShaderID shader, const std::string& param_name, const Args&... args)
    {
        bind_shader(shader);

        if constexpr (ParamConcept::AllFloat<Args...>)
        {
            set_param_impl(shader, param_name, static_cast<float>(args)...);
        }
        else if constexpr (ParamConcept::AllInt<Args...>)
        {
            set_param_impl(shader, param_name, static_cast<int>(args)...);
        }
        else if constexpr (ParamConcept::AllBool<Args...>)
        {
            set_param_impl(shader, param_name, static_cast<bool>(args)...);
        }
    }

    template<typename IDVec>
    requires ParamConcept::IsVec<IDVec>
    void set_param(const PipelineID pipeline, const std::string& param_name, const IDVec& vec)
    {
        bind_pipeline(pipeline);

        if constexpr (std::same_as<IDVec, Vec2>)
        {
            set_param_impl(pipeline, param_name, vec[0], vec[1]);
        }
        else if constexpr (std::same_as<IDVec, Vec3>)
        {
            set_param_impl(pipeline, param_name, vec[0], vec[1], vec[2]);
        }
        else if constexpr (std::same_as<IDVec, Vec4>)
        {
            set_param_impl(pipeline, param_name, vec[0], vec[1], vec[2], vec[3]);
        }
    }

    template<typename IDVec>
    requires ParamConcept::IsVec<IDVec>
    void set_param(const ShaderID shader, const std::string& param_name, const IDVec& vec)
    {
        bind_shader(shader);

        if constexpr (std::same_as<IDVec, Vec2>)
        {
            set_param_impl(shader, param_name, vec[0], vec[1]);
        }
        else if constexpr (std::same_as<IDVec, Vec3>)
        {
            set_param_impl(shader, param_name, vec[0], vec[1], vec[2]);
        }
        else if constexpr (std::same_as<IDVec, Vec4>)
        {
            set_param_impl(shader, param_name, vec[0], vec[1], vec[2], vec[3]);
        }
    }

    template<typename IDMat>
    requires ParamConcept::IsMat<IDMat>
    void set_param(const PipelineID pipeline, const std::string& param_name, const IDMat& mat)
    {
        bind_pipeline(pipeline);
        set_param_impl(pipeline, param_name, mat);
    }

    template<typename IDMat>
    requires ParamConcept::IsMat<IDMat>
    void set_param(const ShaderID shader, const std::string& param_name, const IDMat& mat)
    {
        bind_shader(shader);
        set_param_impl(shader, param_name, mat);
    }
} // namespace ID::RenderCommand
