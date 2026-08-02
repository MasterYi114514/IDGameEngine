#include "Resource/Shader/ShaderSourceLoader.hpp"
#include "Log/Log.hpp"

namespace ID
{
    std::string ShaderSourceLoader::load_shader_source(const std::filesystem::path& shader_path)
    {
        if(!std::filesystem::exists(shader_path))
        {
            IDR_ERROR("路径 {} 下没有找到着色器源文件，请检查路径是否正确", shader_path.string().c_str());
            return "";
        }

        std::ifstream shader_file(shader_path);
        if(!shader_file.is_open())
        {
            IDR_ERROR("无法打开着色器源文件 {}，请检查文件是否被占用或权限是否正确", shader_path.string().c_str());
            return "";
        }

        std::stringstream ssss;          // shader source string stream
        ssss << shader_file.rdbuf();
        return ssss.str();
    }
} // namespace ID